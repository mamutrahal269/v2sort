#include "configgen.hpp"
#include "geodata.hpp"
#include "net.hpp"
#include "utils.hpp"
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
#include <cerrno>
#include <chrono>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <ranges>
#include <regex>
#include <string>
#include <thread>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

#ifdef _WIN32

#include <io.h>
#include <windows.h>
#define PLATFORM_STDOUT "CONOUT$"
#define PLATFORM_STDERR "CONERR$"
#define PLATFORM_NULL "NUL"

#else

#include <unistd.h>
#define PLATFORM_STDOUT "/dev/stdout"
#define PLATFORM_STDERR "/dev/stderr"
#define PLATFORM_NULL "/dev/null"

#endif

std::string str_tolower(const std::string& s) {
	std::string lower;
	std::ranges::for_each(s, [&lower](char c) { lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
	return lower;
}
struct proxy_report {
	std::string		url;
	geodata			geo;
	connection_info net;
};
using namespace boost;
struct v2sort_params {
	std::string				   config;
	std::string				   list;
	size_t					   nthreads;
	uint16_t				   start_port;
	size_t					   xray_wait;
	size_t					   timeout;
	std::string				   output;
	std::optional<std::string> regex;
	std::optional<size_t>	   ppt;
	bool					   verbose;
	bool					   random;
	std::optional<std::string> mmdb_path;
	bool					   ipv4;
	bool					   ipv6;
};
int main(int argc, char* argv[]) {
	v2sort_params params{};

	CLI::App app("v2sort");
	app.add_option("-c,--config", params.config, "path to the configuration file")->required()->check(CLI::ExistingFile);
	app.add_option("-l,--list", params.list, "file with proxy URLs")->required()->check(CLI::ExistingFile);
	app.add_option("-j,--jobs", params.nthreads, "number of threads")->check(CLI::Range((size_t) 1, (size_t) ~0))->default_val(1);
	app.add_option("-p,--port", params.start_port, "starting port for local socks5")->check(CLI::Range(1, 65535))->default_val(10808);
	app.add_option("-w,--wait", params.xray_wait, "xray wait time after launch")->check(CLI::PositiveNumber)->default_val(1500);
	app.add_option("-T,--timeout", params.timeout, "timeout for all network operations")->check(CLI::PositiveNumber)->default_val(5);
	app.add_option("-o,--output", params.output, "?");
	app.add_option("-R,--regex", params.regex, "regular expression for proxy extraction");
	app.add_option("-P,--proxy_per_test", params.ppt, "number of proxies tested at a time")
		->check(CLI::PositiveNumber)
		->check(CLI::Range(1, 65535));
#ifdef MMDB_SUPPORTED
	app.add_option("-m,--mmdb", params.mmdb_path, "path to the mmdb file")->check(CLI::ExistingFile);
#endif

	app.add_flag("-v,--verbose", params.verbose, "output debugging information");
	app.add_flag("-r,--random", params.random, "select 1 random element from setting.urls instead of using all");
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
	sink->set_formatter(log::expressions::stream
						<< argv[0] << ": "
						<< "[" << log::trivial::severity << "]\t"
						<< "<" << log::expressions::attr<log::attributes::current_thread_id::value_type>("ThreadID") << "> "
						<< log::expressions::message);
	if (!params.verbose) {
		sink->set_filter(log::trivial::severity >= log::trivial::warning);
	}
	log::core::get()->add_sink(sink);
	log::add_common_attributes();

	toml::table conf = toml::parse(read_file(params.config));

	std::thread				  threads[params.nthreads];
	std::vector<json::object> result;

	{
		json::object frags[params.nthreads];
		std::fill_n(frags, params.nthreads, EMPTY_XRAY_CONF);
		std::string text;
		try {
			text = read_file(params.list);
		} catch (const std::ios_base::failure&) {
			const auto e = errno;
			BOOST_LOG_TRIVIAL(fatal) << params.list << ": " << std::strerror(e) << '\n';
			return e;
		}
		std::vector<std::smatch> matches = extract_proxies(params.regex.has_value() ? params.regex.value().c_str() : nullptr, text);
		/* removing unnecessary proxies, in accordance with the configuration */
		{
			bool	   whitelist = conf["settings"]["protocols"]["whitelist"].value_or(false);
			const auto protocols = conf["settings"]["protocols"]["list"].as_array();
			if (!protocols) {
				BOOST_LOG_TRIVIAL(error) << "couldn't access settings.protocols.list as array\n";
			} else {
				matches.erase(std::remove_if(matches.begin(), matches.end(),
											 [&](const std::smatch& m) {
												 for (const auto& p : *protocols) {
													 const auto str = p.as_string();
													 if (!str) {
														 BOOST_LOG_TRIVIAL(error) << "couldn't access settings.protocols.list[X] as "
																					 "string\n";
														 return false; /* allow by default */
													 }
													 if (str_tolower(str->get()) == str_tolower(m[1])) return !whitelist;
												 }
												 return whitelist;
											 }),
							  matches.end());
			}
		}
		if (matches.empty()) return 0;
		params.nthreads = std::min(params.nthreads, matches.size());

		/* starting generators */
		for (int i = 0; i < params.nthreads; ++i) {
			threads[i] = std::thread([&, i] {
				auto					 start_end_index = subrange((size_t) 0, (size_t) matches.size() - 1, params.nthreads, i);
				std::vector<std::string> thread_configs;
				for (size_t i = start_end_index.first; i < (start_end_index.second + 1); ++i) {
					auto s = matches[i][0].str();
					s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return std::isspace(c); }), s.end());
					thread_configs.push_back(s);
				}
				try {
					frags[i] = configgen(thread_configs);
				} catch (const std::exception& e) {
					BOOST_LOG_TRIVIAL(error) << e.what() << '\n';
				} catch (...) {
					BOOST_LOG_TRIVIAL(error) << "unknown error\n";
				}
				BOOST_LOG_TRIVIAL(debug) << frags[i] << '\n';
			});
		}
		for (int i = 0; i < params.nthreads; ++i) threads[i].join();

		result = refragment_configs(frags, params.nthreads, params.ppt.has_value() ? params.ppt.value() : UINT16_MAX - params.start_port);
	}
	{
		std::string access		 = conf["xray"]["log"]["access"].value_or(PLATFORM_STDOUT);
		std::string error		 = conf["xray"]["log"]["error"].value_or(PLATFORM_STDERR);
		std::string log_level	 = conf["xray"]["log"]["log_level"].value_or("warning");
		bool		dns_log		 = conf["xray"]["log"]["dns_log"].value_or(false);
		std::string mask_address = conf["xray"]["log"]["mask_address"].value_or("");
		for (auto& v : result) {
			add_log_obj(v, access, error, log_level, dns_log, mask_address);
			fill_ports(v, params.start_port);
			fill_tags(v);
		}
	}
	curl_global_init(CURL_GLOBAL_DEFAULT);

	std::vector<proxy_report> reports;
	std::mutex				  rep_mtx;

	std::string xray_path = conf["xray"]["path"].value_or("");
	if (xray_path.empty()) {
		xray_path = boost::process::search_path("xray").string();
		if (xray_path.empty()) {
			BOOST_LOG_TRIVIAL(fatal) << "xray executable file not found\n";
			return ENOENT;
		}
	}
#ifdef MMDB_SUPPORTED
	MMDB_s mmdb;
	if (params.mmdb_path) {
		int status;
		if ((status = MMDB_open(params.mmdb_path.value().c_str(), MMDB_MODE_MMAP, &mmdb)) != MMDB_SUCCESS) {
			BOOST_LOG_TRIVIAL(fatal) << params.mmdb_path.value() << ": " << MMDB_strerror(status) << '\n';
			return status;
		}
	}
#endif
	try {
		for (const auto& cur_obj : result) {
			process::opstream xray_stdin;
			process::child	  xray(xray_path, "run", process::std_out > conf["xray"]["stdout"].value_or(PLATFORM_STDOUT),
								   process::std_err > conf["xray"]["stderr"].value_or(PLATFORM_STDERR), process::std_in < xray_stdin);
			xray_stdin << boost::json::serialize(cur_obj);
			xray_stdin.flush();
			xray_stdin.pipe().close();

			if (xray.wait_for(std::chrono::milliseconds{params.xray_wait})) {
				BOOST_LOG_TRIVIAL(fatal) << "process " << conf["xray"]["path"].value_or("/usr/local/bin/xray") << " exited with code '"
										 << xray.exit_code() << "'\n";
				return 1;
			}
			params.nthreads = std::min(params.nthreads, cur_obj.at("inbounds").as_array().size());
			const auto urls = conf["settings"]["urls"].as_array();
			if (!urls) {
				BOOST_LOG_TRIVIAL(fatal) << "could not access to settings.urls as array\n";
				return 1;
			}
			for (int i = 0; i < params.nthreads; ++i) {
				threads[i] = std::thread([&, i] {
					std::vector<proxy_report>			  local_reports(1);
					std::random_device					  rd;
					std::mt19937						  gen(rd());
					std::uniform_int_distribution<size_t> dist(0, urls->size() - 1);
					auto indx_range = subrange((size_t) 0, (size_t) cur_obj.at("inbounds").as_array().size() - 1, params.nthreads, i);
					int	 flags		= (params.ipv4 ? NET_IPV4_ONLY : 0) | (params.ipv6 ? NET_IPV6_ONLY : 0);

					for (const auto j :
						 std::ranges::iota_view(static_cast<unsigned>(indx_range.first), static_cast<unsigned>(indx_range.second + 1))) {
						local_reports.push_back({});
						local_reports.back().url = cur_obj.at("inbounds").as_array()[j].at("src").as_string().c_str();
						size_t port				 = cur_obj.at("inbounds").as_array()[j].at("port").as_uint64();
						if (params.random) {
							const auto rand_url = (urls->begin()[dist(gen)]).as_string();
							if (!rand_url) {
								BOOST_LOG_TRIVIAL(fatal) << "could not access to settings.urls[X] as string\n";
								return 1;
							}
							if (const auto ret = httpcheck(port, rand_url->get(), params.timeout, flags)) {
								local_reports.back().net = ret.value();
							} else {
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
									local_reports.back().net.t_dns += ret.value().t_dns;
									local_reports.back().net.t_connect += ret.value().t_connect;
									local_reports.back().net.t_tls += ret.value().t_tls;
									local_reports.back().net.t_ttfb += ret.value().t_ttfb;
									local_reports.back().net.t_total += ret.value().t_total;
									local_reports.back().net.speed += ret.value().speed;
									local_reports.back().net.size += ret.value().size;
								} else {
									BOOST_LOG_TRIVIAL(error) << u->get() << ": " << ret.error().message();
								}
							}
							local_reports.back().net.http_code = 0;
							local_reports.back().net.t_dns /= urls->size();
							local_reports.back().net.t_connect /= urls->size();
							local_reports.back().net.t_tls /= urls->size();
							local_reports.back().net.t_ttfb /= urls->size();
							local_reports.back().net.speed /= urls->size();
						}
						if (!params.mmdb_path) {
							if (const auto ret = ipinfo_geodata(port, params.timeout, flags)) {
								local_reports.back().geo = ret.value();
							} else {
								BOOST_LOG_TRIVIAL(error) << "ipinfo.io: " << ret.error().message() << '\n';
							}
						} else {
							std::string addr =
								cur_obj.at("outbounds")
									.as_array()[j]
									.at("settings")
									.as_object()
									.at(cur_obj.at("outbounds").as_array()[j].at("settings").as_object().contains("servers") ? "servers"
																															 : "vnext")
									.as_array()[0]
									.as_object()
									.at("address")
									.as_string()
									.c_str();
							if (addr.front() == '[' && addr.back() == ']') {
								addr.erase(addr.begin());
								addr.pop_back();
							}
							if (const auto ret = mmdb_geodata(mmdb, addr)) {
								local_reports.back().geo = ret.value();
							} else {
								BOOST_LOG_TRIVIAL(error)
									<< params.mmdb_path.value() << ": " << addr << ": " << ret.error().message() << '\n';
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
									 [whitelist, list](const proxy_report& r) {
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
									 [whitelist, list](const proxy_report& r) {
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
	for (const auto& r : reports) {
		if (r.net.size == 0) continue;
		std::cout << "==================================\n"
				  << "URL: " << r.url << '\n'
				  << "IP: " << r.geo.ip << '\n'
				  << "Country: " << r.geo.country << '\n'
				  << "----------------------------------\n"
				  << "HTTP code: " << r.net.http_code << '\n'
				  << "t_dns: " << r.net.t_dns * 1000 << " ms\n"
				  << "t_connect: " << r.net.t_connect * 1000 << " ms\n"
				  << "t_tls: " << r.net.t_tls * 1000 << " ms\n"
				  << "t_ttfb: " << r.net.t_ttfb * 1000 << " ms\n"
				  << "t_total: " << r.net.t_total * 1000 << " ms\n"
				  << "speed: " << (float) r.net.speed / 1024 << "K\n"
				  << "size: " << (float) r.net.size / 1024 << "K\n"
				  << "==================================\n";
	}
	return 0;
}
