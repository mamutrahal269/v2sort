#include <curl/curl.h>
#include <errno.h>
#include <getopt.h>
#include <pwd.h>
#include <unistd.h>

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
#include <chrono>
#include <ext/stdio_filebuf.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <ranges>
#include <regex>
#include <string>
#include <syncstream>
#include <thread>
#include <toml++/toml.hpp>
#include <utility>
#include <vector>

#include "configgen.hpp"
#include "net.hpp"
#include "utils.hpp"

#define PARAMS_BUFFER UCHAR_MAX
#define DEFAULT_PROXY_PORT 10808U

/* functions for extracting command line parameters */
template <typename T> inline T param_or(const char* const params[], unsigned char indx, T def) {
	if (!params[indx]) return def;

	if constexpr (std::is_same_v<T, const char*> || std::is_same_v<T, std::string>) {
		return params[indx];
	} else if constexpr (std::is_integral_v<T>) {
		return static_cast<T>(std::stoll(params[indx]));
	} else if constexpr (std::is_floating_point_v<T>) {
		return static_cast<T>(std::stod(params[indx]));
	} else {
		static_assert(!sizeof(T*), "Unsupported type for param_or");
	}
}
inline const char* param_or(const char* const params[], unsigned char indx, std::nullptr_t) noexcept {
	return params[indx];
}

std::string str_tolower(const std::string& s) {
	std::string lower;
	std::ranges::for_each(s, [&lower](char c) { lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
	return lower;
}
struct proxy_report {
	std::string		url;
	ipinfo_proxy	geo;
	connection_info net;
};
using namespace boost;
void usage() {};
int	 main(int argc, char* argv[]) {
	 char* params[PARAMS_BUFFER] = {};
	 {
		 option options[] = {{"config", required_argument, nullptr, 'c'},
							 {"list", required_argument, nullptr, 'l'},
							 {"jobs", required_argument, nullptr, 'j'},
							 {"port", required_argument, nullptr, 'p'},
							 {"verbose", no_argument, nullptr, 'v'},
							 {"random", no_argument, nullptr, 'r'},
							 {"help", no_argument, nullptr, 'h'},
							 {"wait", required_argument, nullptr, 'w'},
							 {"timeout", required_argument, nullptr, 'T'},
							 {"output", required_argument, nullptr, 'o'},
							 {"regex", required_argument, nullptr, 'R'},
							 {"proxy_per_test", required_argument, nullptr, 'P'},
							 {"ipv4_only", no_argument, nullptr, '4'},
							 {"ipv6_only", no_argument, nullptr, '6'},
							 {0, 0, 0, 0}};
		 opterr			  = 1;
		 int opt;
		 int option_index = 0;
		 while ((opt = getopt_long(argc, argv, "c:l:j:p:vrhw:T:o:R:P:46", options, &option_index)) != -1) {
			 if (opt == '?') continue;
			 params[(unsigned char) opt] = optarg ? optarg : (char*) 1;
		 }
	 }

	 /* log setup */
	 using asink_t = log::sinks::asynchronous_sink<log::sinks::text_ostream_backend>;
	 auto backend  = make_shared<log::sinks::text_ostream_backend>();
	 backend->add_stream(shared_ptr<std::ostream>(&std::clog, null_deleter()));
	 auto sink = make_shared<asink_t>(backend);
	 sink->set_formatter(log::expressions::stream << argv[0] << ": [" << log::trivial::severity << "]\t" << log::expressions::message);
	 if (!param_or(params, 'v', nullptr)) {
		 sink->set_filter(log::trivial::severity >= log::trivial::warning);
	 }
	 log::core::get()->add_sink(sink);

	 /* help */
	 if (param_or(params, 'h', nullptr)) {
		 usage();
		 return 0;
	 }

	 /* getting settings */
	 toml::table conf;
	 {
		 std::string path_to_config;
		 if (!param_or(params, 'c', nullptr)) {
			 const char* home = getenv("HOME");
			 /* getpwuid if getenv failed */
			 if (!home) {
				 passwd* pw = getpwuid(getuid());
				 if (pw) home = pw->pw_dir;
			 }
			 /* If nothing succeeds, indicate the known path (/etc/v2sort/config) */
			 path_to_config = home ? (std::string(home) + "/.v2sort") : "/etc/v2sort/config";
		 } else
			 path_to_config = params[(unsigned char) 'c'];
		 /* file reading, parsing and error handling */
		 try {
			 conf = toml::parse(read_file(path_to_config));
		 } catch (const toml::parse_error& e) {
			 BOOST_LOG_TRIVIAL(error) << path_to_config << ":" << e.source().begin.line << ":" << e.source().begin.column << ": "
									  << e.description() << '\n';
		 } catch (const std::ios_base::failure&) {
			 const auto e = errno;
			 BOOST_LOG_TRIVIAL(error) << path_to_config << ": " << std::strerror(e) << '\n';
		 }
	 }

	 size_t nthreads;
	 try {
		 nthreads = param_or(params, 'j', 1);
	 } catch (...) {
		 nthreads = 1;
	 }
	 if (!nthreads) nthreads = 1;
	 std::thread			   threads[nthreads];
	 std::vector<json::object> result;

	 {
		 json::object frags[nthreads];
		 std::fill_n(frags, nthreads, EMPTY_XRAY_CONF);
		 if (!param_or(params, 'l', nullptr)) {
			 usage();
			 return 1;
		 }
		 std::string text;
		 try {
			 text = read_file(param_or(params, 'l', nullptr));
		 } catch (const std::ios_base::failure&) {
			 const auto e = errno;
			 BOOST_LOG_TRIVIAL(fatal) << param_or(params, 'l', nullptr) << ": " << std::strerror(e) << '\n';
			 return e;
		 }
		 std::vector<std::smatch> matches = extract_proxies(param_or(params, 'R', nullptr), text);
		 /* removing unnecessary proxies, in accordance with the configuration */
		 {
			 bool		whitelist = conf["settings"]["protocols"]["whitelist"].value_or(false);
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
		 nthreads = std::min(nthreads, matches.size());

		 /* starting generators */
		 for (int i = 0; i < nthreads; ++i) {
			 threads[i] = std::thread([&, i] {
				 auto					  start_end_index = subrange((size_t) 0, (size_t) matches.size() - 1, nthreads, i);
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
		 for (int i = 0; i < nthreads; ++i) threads[i].join();

		 result = refragment_configs(frags, nthreads, UINT16_MAX - param_or(params, 'p', DEFAULT_PROXY_PORT));
	 }
	 {
		 std::string access		  = conf["xray"]["log"]["access"].value_or("/dev/stdout");
		 std::string error		  = conf["xray"]["log"]["error"].value_or("/dev/stdout");
		 std::string log_level	  = conf["xray"]["log"]["log_level"].value_or("warning");
		 bool		 dns_log	  = conf["xray"]["log"]["dns_log"].value_or(false);
		 std::string mask_address = conf["xray"]["log"]["mask_address"].value_or("");
		 for (auto& v : result) {
			 add_log_obj(v, access, error, log_level, dns_log, mask_address);
			 fill_ports(v, param_or(params, 'p', DEFAULT_PROXY_PORT));
			 fill_tags(v);
		 }
	 }
	 /* getting the temporary files directory */
	 std::filesystem::path tmpdir, tmppath;
	 try {
		 tmpdir = std::filesystem::temp_directory_path();
	 } catch (const std::filesystem::filesystem_error& e) {
		 BOOST_LOG_TRIVIAL(fatal) << e.path1() << ": " << e.code().message() << '\n';
		 return e.code().value();
	 }
	 /* name generation */
	 {
		 std::random_device						  rd;
		 std::mt19937							  gen(rd());
		 std::uniform_int_distribution<uintmax_t> dist;
		 do {
			 tmppath = tmpdir / (std::to_string(dist(gen)) + ".json");
		 } while (std::filesystem::exists(tmppath));
	 }

	 /* Opening a file with open() to obtain
		a file descriptor used in ftruncate() */
	 const int tmp_fd = open(tmppath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0600);
	 if (tmp_fd == -1) {
		 const auto e = errno;
		 BOOST_LOG_TRIVIAL(fatal) << tmppath << ": " << strerror(e) << '\n';
		 return e;
	 }
	 __gnu_cxx::stdio_filebuf<char> fb(tmp_fd, std::ios::out);
	 if (!fb.is_open()) {
		 const auto e = errno;
		 BOOST_LOG_TRIVIAL(fatal) << tmppath << ": " << strerror(e) << '\n';
		 return e;
	 }
	 std::ostream tmp(&fb);
	 tmp.exceptions(std::ios::failbit | std::ios::badbit);

	 curl_global_init(CURL_GLOBAL_DEFAULT);

	 std::vector<proxy_report> reports;
	 std::mutex				   rep_mtx;
	 for (const auto& cur_obj : result) {
		 if (ftruncate(tmp_fd, 0)) {
			 const auto e = errno;
			 BOOST_LOG_TRIVIAL(fatal) << tmppath << ": " << strerror(e) << '\n';
			 return e;
		 }
         tmp.seekp(0);
		 try {
			 tmp << cur_obj << std::endl;
		 } catch (const std::ios_base::failure&) {
			 const auto e = errno;
			 BOOST_LOG_TRIVIAL(fatal) << tmppath << ": " << strerror(e) << '\n';
			 return e;
		 }

		 process::child xray(conf["xray"]["path"].value_or("/usr/local/bin/xray"), "run", "-config", tmppath.c_str(),
							 process::std_out > conf["xray"]["stdout"].value_or("/dev/stdout"),
							 process::std_err > conf["xray"]["stderr"].value_or("/dev/stderr"));
		 if (xray.wait_for(std::chrono::milliseconds{param_or(params, 'w', 3500)})) {
			 BOOST_LOG_TRIVIAL(fatal) << "process " << conf["xray"]["path"].value_or("/usr/local/bin/xray") << " exited with code '"
									  << xray.exit_code() << "'\n";
			 return 1;
		 }
		 nthreads		 = std::min(nthreads, cur_obj.at("inbounds").as_array().size());
		 const auto urls = conf["settings"]["urls"].as_array();
		 if (!urls) {
			 BOOST_LOG_TRIVIAL(fatal) << "could not access to settings.urls as array\n";
			 return 1;
		 }
		 for (int i = 0; i < nthreads; ++i) {
			 threads[i] = std::thread([&, i] {
				 std::vector<proxy_report>			   local_reports(1);
				 std::random_device					   rd;
				 std::mt19937						   gen(rd());
				 std::uniform_int_distribution<size_t> dist(0, urls->size() - 1);
				 auto indx_range = subrange((size_t) 0, (size_t) cur_obj.at("inbounds").as_array().size() - 1, nthreads, i);
				 int  flags = (param_or(params, '4', nullptr) ? NET_IPV4_ONLY : 0) | (param_or(params, '6', nullptr) ? NET_IPV6_ONLY : 0);

				 for (const auto j :
					  std::ranges::iota_view(static_cast<unsigned>(indx_range.first), static_cast<unsigned>(indx_range.second + 1))) {
					 local_reports.back().url = cur_obj.at("inbounds").as_array()[j].at("src").as_string().c_str();
					 size_t port			  = cur_obj.at("inbounds").as_array()[j].at("port").as_uint64();
					 if (param_or(params, 'r', nullptr)) {
						 const auto rand_url = (urls->begin()[dist(gen)]).as_string();
						 if (!rand_url) {
							 BOOST_LOG_TRIVIAL(fatal) << "could not access to settings.urls[X] as string\n";
							 return 1;
						 }
						 uint32_t timeout;
						 try {
							 timeout = param_or(params, 'T', 5);
						 } catch (...) {
							 timeout = 5;
						 }
						 local_reports.back().net = httpcheck(port, rand_url->get(), timeout, flags);
					 } else {
						 for (const auto& val : *urls) {
							 const auto u = val.as_string();
							 if (!u) {
								 BOOST_LOG_TRIVIAL(fatal) << "could not access to settings.urls[X] as string\n";
								 return 1;
							 }
							 uint32_t timeout;
							 try {
								 timeout = param_or(params, 'T', 5);
							 } catch (...) {
								 timeout = 5;
							 }
							 auto ci1 = httpcheck(port, u->get(), timeout, flags);

							 local_reports.back().net.t_dns += ci1.t_dns;
							 local_reports.back().net.t_connect += ci1.t_connect;
							 local_reports.back().net.t_tls += ci1.t_tls;
							 local_reports.back().net.t_ttfb += ci1.t_ttfb;
							 local_reports.back().net.t_total += ci1.t_total;
							 local_reports.back().net.speed += ci1.speed;
							 local_reports.back().net.size += ci1.size;
						 }
						 local_reports.back().net.http_code = 0;
						 local_reports.back().net.t_dns /= urls->size();
						 local_reports.back().net.t_connect /= urls->size();
						 local_reports.back().net.t_tls /= urls->size();
						 local_reports.back().net.t_ttfb /= urls->size();
						 local_reports.back().net.t_total /= urls->size();
						 local_reports.back().net.speed /= urls->size();
						 local_reports.back().net.size /= urls->size();
					 }
					 uint32_t timeout;
					 try {
						 timeout = param_or(params, 'T', 5);
					 } catch (...) {
						 timeout = 5;
					 }
					 local_reports.back().geo = ipinfo(port, timeout, flags);
					 local_reports.push_back({});
				 }
				 std::lock_guard<std::mutex> lk(rep_mtx);
				 reports.reserve(reports.size() + local_reports.size());
				 reports.insert(reports.end(), std::make_move_iterator(local_reports.begin()), std::make_move_iterator(local_reports.end()));
				 return 0;
			 });
		 }
		 for (int i = 0; i < nthreads; ++i) threads[i].join();
		 xray.terminate();
		 xray.wait();
	 }
	 curl_global_cleanup();
	 bool				whitelist = conf["settings"]["countries"]["whitelist"].value_or(false);
	 const toml::array* list	  = conf["settings"]["countries"]["list"].as_array();
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
	 list	   = conf["settings"]["ips"]["list"].as_array();
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
	 for (const auto& r : reports) {
		 if (r.geo.ip == "" && r.geo.country == "" && r.geo.region == "" && r.geo.city == "" && !r.net.http_code && !r.net.t_total) continue;
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
				   << "speed: " << r.net.speed / 1024 << "K\n"
				   << "size: " << r.net.size / 1024 << "K\n"
				   << "==================================\n";
	 }
	 return 0;
}
