#include "fmt_reports.hpp"
#include "geodata.hpp"
#include "net.hpp"
#include "urls_configgen.hpp"
#include "utils.hpp"
#include "v2sort.hpp"
#include <CLI/CLI.hpp>
#include <boost/core/null_deleter.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/make_shared.hpp>
#include <boost/process.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/url.hpp>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <ranges>
#include <regex>
#include <string>
#include <thread>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

using namespace boost;
static const std::map<std::string, out_style>	style_map{{"raw", out_style::raw}, {"json", out_style::json}, {"human", out_style::human}};
static const std::map<std::string, geo_service> service_map{
#ifdef MMDB_SUPPORTED
	{"mmdb", geo_service::mmdb},
#endif
    {"cdn_cgi", geo_service::cdn_cgi}, {"ipinfo", geo_service::ipinfo}};

int main(int argc, char* argv[]) {
	v2sort_params params{};

	CLI::App app("v2sort");
	app.add_option("-c,--config", params.config, "path to the configuration file")->required()->check(CLI::ExistingFile);
	app.add_option("-l,--list", params.list, "file(s) and/or url(s) with proxy URLs")->required()->delimiter(',');
	app.add_option("-j,--jobs", params.nthreads, "number of threads")->check(CLI::Range((size_t) 1, (size_t) ~0))->default_val(1);
	app.add_option("-p,--port", params.start_port, "starting port for local socks5")->check(CLI::Range(1, 65535))->default_val(10808);
	app.add_option("-w,--wait", params.xray_wait, "xray wait time after launch")->check(CLI::PositiveNumber)->default_val(1500);
	app.add_option("-T,--timeout", params.timeout, "timeout for all network operations")->check(CLI::PositiveNumber)->default_val(5);
	app.add_option("-g,--geo_service", params.service, "service for obtaining geodata")
		->transform(CLI::CheckedTransformer(service_map, CLI::ignore_case))
		->default_str("ipinfo");
	auto opt_output = app.add_option("-o,--output", params.output, "report file")->default_val(PLATFORM_STDOUT);
	app.add_option("-s,--style", params.style, "reporting style")
		->transform(CLI::CheckedTransformer(style_map, CLI::ignore_case))
		->default_val(out_style::raw);
	auto opt_bad = app.add_option("-b,--bad", params.bad, "file for invalid and non-working proxy URLs");
	app.add_option("-R,--regex", params.regex, "regular expression for proxy extraction")
		->default_val(R"((?:^|\s)([a-zA-Z][a-zA-Z0-9+.-]*)://([^\s]+))");
	app.add_option("-P,--proxy_per_test", params.ppt, "number of proxies tested at a time")
		->check(CLI::PositiveNumber)
		->check(CLI::Range(1, 65535));
	app.add_option("-F,--fragment", params.fragment_format, "formatting a fragment for each proxy URL");
	app.add_option("-C,--xray_conf", params.xray_conf, "write configurations to file(s) without network tests");
#ifdef MMDB_SUPPORTED
	app.add_option("-m,--mmdb", params.mmdb_path, "path to the mmdb file")->check(CLI::ExistingFile);
#endif

	app.add_flag("-n,--no_geo", params.no_geo, "do not receive geodata");
	app.add_flag("--trunc_report", params.trunc_report, "truncate the report file")->needs(opt_output);
	app.add_flag("--trunc_bad", params.trunc_bad, "truncate the file specified in --bad")->needs(opt_bad);
	app.add_flag("-v,--verbose", params.verbose, "output debugging information");
	app.add_flag("-S,--speedtest", params.speedtest, "run speedtests");
	app.add_flag("-r,--random", params.random, "select 1 random element from settings.urls instead of using all");
	auto f4 = app.add_flag("-4,--ipv4_only", params.ipv4, "use only ipv4 for all network operations");
	auto f6 = app.add_flag("-6,--ipv6_only", params.ipv6, "use only ipv6 for all network operations");
	f4->excludes(f6);
	f6->excludes(f4);

	CLI11_PARSE(app, argc, argv);
	/* log setup */
	using asink_t = log::sinks::asynchronous_sink<log::sinks::text_ostream_backend>;
	auto backend  = make_shared<log::sinks::text_ostream_backend>();
	backend->add_stream(shared_ptr<std::ostream>(&std::clog, null_deleter()));
	auto sink = make_shared<asink_t>(backend);
	sink->set_formatter(log::expressions::stream << argv[0] << ": "
												 << "[" << log::trivial::severity << "]\t" << log::expressions::message);
	if (!params.verbose) {
		sink->set_filter(log::trivial::severity >= log::trivial::warning);
	}
	log::core::get()->add_sink(sink);
	log::add_common_attributes();

	toml::table conf = toml::parse(read_file(params.config));

	std::thread				  threads[params.nthreads];
	std::vector<json::object> result;
	curl_global_init(CURL_GLOBAL_DEFAULT);

	std::vector<std::string> bad_list;
	try {
		result = urls_configgen_auto(params.list, conf, params, bad_list);
	} catch (const std::ios_base::failure&) {
		const auto e = errno;
		BOOST_LOG_TRIVIAL(fatal) << "error: " << std::strerror(e) << '\n';
		return e;
	} catch (const std::system_error& e) {
		BOOST_LOG_TRIVIAL(fatal) << "error: " << e.what() << '\n';
		return e.code().value();
	}
	std::string xray_path = conf["xray"]["path"].value_or("");
	if (xray_path.empty()) {
		xray_path = boost::process::search_path("xray").string();
		if (xray_path.empty()) {
			BOOST_LOG_TRIVIAL(fatal) << "xray executable file not found\n";
			return ENOENT;
		}
	}
	if (params.xray_conf) {
		std::ofstream f;
		f.exceptions(std::ios_base::failbit | std::ios_base::badbit);
		size_t i;
		try {
			for (i = 0; i < result.size(); ++i) {
				f.open(std::to_string(i) + params.xray_conf.value(), std::ios::trunc | std::ios::out);
				f << result[i];
				f.close();
			}
		} catch (const std::ios_base::failure&) {
			const auto e = errno;
			BOOST_LOG_TRIVIAL(fatal) << std::to_string(i) + params.xray_conf.value() << ": " << std::strerror(e) << '\n';
			return e;
		}
		return 0;
	}

	std::vector<proxy_report> reports;
	std::mutex				  rep_mtx;

#ifdef MMDB_SUPPORTED
	MMDB_s mmdb;
	if (params.service == geo_service::mmdb) {
		if (!params.mmdb_path) {
			BOOST_LOG_TRIVIAL(fatal) << "the path to the MMDB file is not specified\n";
			return ENOENT;
		}
		int status;
		if ((status = MMDB_open(params.mmdb_path.value().c_str(), MMDB_MODE_MMAP, &mmdb)) != MMDB_SUCCESS) {
			BOOST_LOG_TRIVIAL(fatal) << params.mmdb_path.value() << ": " << MMDB_strerror(status) << '\n';
			return status;
		}
	}
#endif
	try {
		for (auto& cur_obj : result) {
			while (!cur_obj["outbounds"].as_array().empty()) {
				process::opstream xray_stdin;
				process::ipstream xray_stdout;
				process::ipstream xray_stderr;
				process::child	  xray(xray_path, "-test", process::std_out > xray_stdout, process::std_err > xray_stderr,
									   process::std_in < xray_stdin);
				xray_stdin << cur_obj << std::endl;
				xray_stdin.flush();
				xray_stdin.pipe().close();

				if (!xray.wait_for(std::chrono::milliseconds{params.xray_wait})) {
					BOOST_LOG_TRIVIAL(fatal) << "xray run -test error\n";
					std::ofstream e("/tmp/xray_err_config.json", std::ios::trunc);
					e << cur_obj;
					goto skip_obj;
				}
				if (xray.exit_code()) {
					std::string err;
					std::string out;
					std::string all;

					std::getline(xray_stderr, err, '\0');
					std::getline(xray_stdout, out, '\0');
					all = err + out;

					std::regex	re(R"(failed to build outbound config with tag out_(\d+))");
					std::smatch m;
					if (std::regex_search(all, m, re)) {
						std::string otag = "out_" + m[1].str();
						std::string itag = "in_" + m[1].str();
						for (size_t i = 0; i < cur_obj["outbounds"].as_array().size(); ++i) {
							if (cur_obj["outbounds"].as_array()[i].as_object()["tag"].as_string() == otag) {
								assert(
									cur_obj["outbounds"].as_array()[i].as_object()["tag"].as_string() == otag &&
									cur_obj["inbounds"].as_array()[i].as_object()["tag"].as_string() == itag &&
									cur_obj["routing"].as_object()["rules"].as_array()[i].as_object()["inboundTag"].as_string() == itag &&
									cur_obj["routing"].as_object()["rules"].as_array()[i].as_object()["outboundTag"].as_string() == otag);
								cur_obj["outbounds"].as_array().erase(cur_obj["outbounds"].as_array().begin() + i);
								cur_obj["inbounds"].as_array().erase(cur_obj["inbounds"].as_array().begin() + i);
								cur_obj["routing"].as_object()["rules"].as_array().erase(
									cur_obj["routing"].as_object()["rules"].as_array().begin() + i);
								break;
							}
						}
						continue;
					} else {
						goto skip_obj;
					}
				} else
					goto xray_test_ok;
			}
		skip_obj:
			continue;
		xray_test_ok:
			process::opstream xray_stdin;
			process::child	  xray(xray_path, "run", process::std_out > conf["xray"]["stdout"].value_or(PLATFORM_STDOUT),
								   process::std_err > conf["xray"]["stderr"].value_or(PLATFORM_STDERR), process::std_in < xray_stdin);
			xray_stdin << cur_obj;
			xray_stdin.flush();
			xray_stdin.pipe().close();

			if (xray.wait_for(std::chrono::milliseconds{params.xray_wait})) {
				BOOST_LOG_TRIVIAL(fatal) << "process " << conf["xray"]["path"].value_or("/usr/local/bin/xray") << " exited with code '"
										 << xray.exit_code() << "'\n";
				continue;
			}

			params.nthreads = std::min(params.nthreads, cur_obj.at("inbounds").as_array().size());
			const auto urls = conf["settings"]["urls"].as_array();
			if (!urls || !urls->size()) {
				BOOST_LOG_TRIVIAL(fatal) << "could not access to settings.urls as array\n";
				return 1;
			}
			for (int i = 0; i < params.nthreads; ++i) {
				threads[i] = std::thread([&, i] {
					std::vector<proxy_report> local_reports(1);
					auto indx_range = subrange((size_t) 0, (size_t) cur_obj.at("inbounds").as_array().size() - 1, params.nthreads, i);
					int	 flags		= (params.ipv4 ? NET_IPV4_ONLY : 0) | (params.ipv6 ? NET_IPV6_ONLY : 0);

					for (const auto j :
						 std::ranges::iota_view(static_cast<unsigned>(indx_range.first), static_cast<unsigned>(indx_range.second + 1))) {
						local_reports.push_back({});
						local_reports.back().url = cur_obj.at("inbounds").as_array()[j].at("src").as_string().c_str();
						size_t port				 = cur_obj.at("inbounds").as_array()[j].at("port").as_uint64();
						if (params.random) {
							srand(time(NULL));
							const auto rand_url = (urls->begin()[rand() % urls->size()]).as_string();
							if (!rand_url) {
								BOOST_LOG_TRIVIAL(fatal) << "could not access to settings.urls[X] as string\n";
								return 1;
							}
							if (const auto ret = httpcheck(port, rand_url->get(), params.timeout, flags)) {
								local_reports.back().net.push_back(ret.value());
							} else {
								local_reports.back().net.push_back({});
								BOOST_LOG_TRIVIAL(error) << rand_url->get() << ": " << ret.error().message();
							}
						} else {
							for (const auto& val : *urls) {
								const auto u = val.as_string();
								if (!u) {
									BOOST_LOG_TRIVIAL(fatal) << "could not access to settings.urls[X] as string\n";
									return 1;
								}
								if (const auto ret = httpcheck(port, u->get(), params.timeout, flags)) {
									local_reports.back().net.push_back(ret.value());
								} else {
									local_reports.back().net.push_back({});
									BOOST_LOG_TRIVIAL(error) << u->get() << ": " << ret.error().message();
								}
							}
						}
						if (!params.no_geo) {
							switch (params.service) {
							case geo_service::ipinfo:
								if (const auto ret = ipinfo_geodata(port, params.timeout, flags)) {
									local_reports.back().geo = ret.value();
								} else {
									BOOST_LOG_TRIVIAL(error) << "ipinfo.io: " << ret.error().message() << '\n';
								}
								break;
							case geo_service::cdn_cgi:
								if (const auto ret =
										cdn_cgi_geodata(port, conf["settings"]["cdn_cgi_host"].value_or("https://www.cloudflare.com"),
														params.timeout, flags)) {
									local_reports.back().geo = ret.value();
								} else {
									BOOST_LOG_TRIVIAL(error) << conf["settings"]["cdn_cgi_host"].value_or("https://www.cloudflare.com")
															 << "/cdn-cgi/trace: " << ret.error().message() << '\n';
								}
								break;
							case geo_service::mmdb:
								std::string ip;
								if (const auto ret =
										cdn_cgi_geodata(port, conf["settings"]["cdn_cgi_host"].value_or("https://www.cloudflare.com"),
														params.timeout, flags)) {
									ip = ret.value().ip;
								} else {
									BOOST_LOG_TRIVIAL(error) << conf["settings"]["cdn_cgi_host"].value_or("https://www.cloudflare.com")
															 << "/cdn-cgi/trace: " << ret.error().message() << '\n';
									break;
								}
								if (ip.empty()) {
									BOOST_LOG_TRIVIAL(error) << "failed to obtain IP\n";
									break;
								}
								if (const auto ret = mmdb_geodata(mmdb, ip)) {
									local_reports.back().geo = ret.value();
								} else {
									BOOST_LOG_TRIVIAL(error)
										<< params.mmdb_path.value() << ": " << ip << ": " << ret.error().message() << '\n';
									break;
								}
								break;
							}
						}
						if (params.speedtest) {
							if (const auto ret = httpcheck(port,
														   conf["settings"]["speedtest_url"].value_or(
															   "http://speed.cloudflare.com/__down?bytes=1048576"), /* 1 MiB */
														   params.timeout, flags)) {
								local_reports.back().speed = ret.value().speed;
							} else {
								BOOST_LOG_TRIVIAL(error)
									<< conf["settings"]["speedtest_url"].value_or("http://speed.cloudflare.com/__down?bytes=1048576")
									<< ret.error().message() << '\n';
							}
						}
					}
					std::lock_guard<std::mutex> lk(rep_mtx);
					reports.reserve(reports.size() + local_reports.size());
					reports.insert(reports.end(), std::make_move_iterator(local_reports.begin()),
								   std::make_move_iterator(local_reports.end()));
					return 0;
				});
			}
			for (int i = 0; i < params.nthreads; ++i) threads[i].join();
			xray.terminate();
			xray.wait();
		}
	} catch (const process::process_error& e) {
		BOOST_LOG_TRIVIAL(fatal) << xray_path << ": " << e.code().message() << '\n';
		return e.code().value();
	} catch (const std::system_error& e) {
		BOOST_LOG_TRIVIAL(fatal) << e.code().message() << '\n';
		return e.code().value();
	} catch (const std::exception& e) {
		BOOST_LOG_TRIVIAL(fatal) << e.what() << '\n';
		return 1;
	}
	curl_global_cleanup();
#ifdef MMDB_SUPPORTED
	if (params.mmdb_path) MMDB_close(&mmdb);
#endif
	bool			   whitelist = conf["settings"]["countries"]["whitelist"].value_or(false);
	const toml::array* list		 = conf["settings"]["countries"]["list"].as_array();
	if (!list) {
		BOOST_LOG_TRIVIAL(error) << "could not access to settings.countries.list as array\n";
	} else {
		reports.erase(std::remove_if(reports.begin(), reports.end(),
									 [&](const proxy_report& r) {
										 for (const auto& c : *list) {
											 const auto str = c.as_string();
											 if (!str)
												 BOOST_LOG_TRIVIAL(error) << "could not access to settings.countries.list[X] as string\n";
											 else if (str_tolower(str->get()) == str_tolower(r.geo.country))
												 return !whitelist;
										 }
										 return whitelist;
									 }),
					  reports.end());
	}
	whitelist = conf["settings"]["ips"]["whitelist"].value_or(false);
	list	  = conf["settings"]["ips"]["list"].as_array();
	if (!list) {
		BOOST_LOG_TRIVIAL(error) << "could not access to settings.ips.list as array\n";
	} else {
		reports.erase(std::remove_if(reports.begin(), reports.end(),
									 [&](const proxy_report& r) {
										 for (const auto& c : *list) {
											 const auto str = c.as_string();
											 if (!str)
												 BOOST_LOG_TRIVIAL(error) << "could not access to settings.ips.list[X] as string\n";
											 else if (str->get() == r.geo.ip)
												 return !whitelist;
										 }
										 return whitelist;
									 }),
					  reports.end());
	}
	sink->stop();
	sink->flush();
	reports.erase(
		std::remove_if(reports.begin(), reports.end(),
					   [&](const proxy_report& r) {
						   if (std::all_of(r.net.begin(), r.net.end(), [](const connection_info& con) { return con.http_code == 0; })) {
							   if (!r.url.empty()) bad_list.push_back(r.url);
							   return true;
						   }
						   return false;
					   }),
		reports.end());
	if (params.speedtest) {
		reports.erase(std::remove_if(reports.begin(), reports.end(),
									 [&](const proxy_report& r) {
										 size_t min_speed = conf["settings"]["min_speed"].value_or(0);
										 if ((!r.speed && min_speed) || r.speed.value() < min_speed) return true;
										 return false;
									 }),
					  reports.end());
	}
	if (params.fragment_format) {
		for (auto& r : reports) {
			r.url = fmt_fragment(r.url, params, r);
		}
	}
	std::ofstream out;
	out.exceptions(std::ios_base::failbit | std::ios_base::badbit);
	try {
		out.open(params.output, (params.trunc_report ? std::ios::trunc : std::ios::app));
		out << str_report(params.style, reports);
	} catch (std::ios_base::failure) {
		const auto e = errno;
		BOOST_LOG_TRIVIAL(fatal) << params.output << ": " << std::strerror(e) << '\n';
		return e;
	}
	if (params.bad) {
		try {
			out.close();
			out.open(params.bad.value(), (params.trunc_bad ? std::ios::trunc : std::ios::app));
			for (const auto& b : bad_list) out << b << '\n';
		} catch (std::ios_base::failure) {
			const auto e = errno;
			BOOST_LOG_TRIVIAL(fatal) << params.bad.value() << ": " << std::strerror(e) << '\n';
			return e;
		}
	}
	return 0;
}
