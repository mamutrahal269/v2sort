#include "urls_configgen.hpp"
#include "mkproto.hpp"
#include "net.hpp"
#include "utils.hpp"
#include "v2sort.hpp"
#include <atomic>
#include <boost/log/trivial.hpp>
#include <boost/url.hpp>
#include <boost/url/encode.hpp>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>
#include <string_view>
#include <thread>
#include <unicode/regex.h>
#include <unicode/uchar.h>
#include <unicode/ucsdet.h>
#include <unicode/unistr.h>
#include <unicode/utypes.h>
#include <unordered_set>
#include <utility>
#include <vector>
using namespace boost;

json::object urls_configgen_core(const std::vector<std::string>& proxies, std::vector<std::string>& bad_proxies,
								 std::function<void(severity_lvl, std::string_view)> msg) {
	json::object out = EMPTY_XRAY_CONF;
	for (const auto& orig : proxies) {
		icu::UnicodeString s = icu::UnicodeString::fromUTF8(orig);
		s.trim();

		auto frag_indx = s.indexOf(u'#');
		if (frag_indx >= 0) s.removeBetween(frag_indx);
		std::string conf;
		for (size_t k = 0; k < s.length(); ++k) {
			UChar32 c = s.char32At(k);
			if (c <= 127) {
				conf += static_cast<char>(c);
			} else {
				std::string u8c;
				icu::UnicodeString(c).toUTF8String(u8c);
				conf += urls::encode(u8c, urls::unreserved_chars);
			}
			if (U_IS_SUPPLEMENTARY(c)) ++k;
		}
		try {
			out["outbounds"].as_array().push_back((conf.starts_with("vless://")	   ? mkvless
												   : conf.starts_with("vmess://")  ? mkvmess
												   : conf.starts_with("ss://")	   ? mkss
												   : conf.starts_with("trojan://") ? mktrojan
												   : conf.starts_with("http://")   ? mkhttp
												   : conf.starts_with("socks")	   ? mksocks
												   : conf.starts_with("hy")		   ? mkhysteria
																				   : throw inval_proto("unsupported protocol"))(orig, ""));
		} catch (const std::exception& e) {
			std::ostringstream m;
			m << "'" << orig << "': " << e.what() << '\n';
			msg(severity_lvl::warning, m.str());
			bad_proxies.push_back(orig);
			continue;
		}
		out["inbounds"].as_array().push_back(json::object{{"tag", ""},
														  {"src", orig},
														  {"listen", "127.0.0.1"},
														  {"protocol", "socks"},
														  {"port", 0},
														  {"settings", json::object{{"auth", "noauth"}, {"udp", true}}}});
		out["routing"].as_object()["rules"].as_array().push_back(json::object{
			{"inboundTag", json::array{""}},
			{"outboundTag", ""},
		});
	}
	return out;
}
std::vector<json::object> urls_configgen_common(std::string_view raw_buffer, const toml::table& conf, const v2sort_params& params,
												std::vector<std::string>&							bad_proxies,
												std::function<void(severity_lvl, std::string_view)> msg) {
	const auto b64_decoded = decode64(raw_buffer.data());
	if (b64_decoded.has_value()) raw_buffer = b64_decoded.value();

	icu::UnicodeString utf_buffer;
	UErrorCode		   icu_status		 = U_ZERO_ERROR;
	UCharsetDetector*  encoding_detector = ucsdet_open(&icu_status);

	const UCharsetMatch* encoding_match = nullptr;
	const char*			 encoding_name	= nullptr;

	if (U_FAILURE(icu_status)) {
		utf_buffer = icu::UnicodeString::fromUTF8(raw_buffer.data());
		icu_status = U_ZERO_ERROR;
		goto icu_skip;
	}

	ucsdet_setText(encoding_detector, raw_buffer.data(), raw_buffer.size(), &icu_status);
	if (U_FAILURE(icu_status)) {
		utf_buffer = icu::UnicodeString::fromUTF8(raw_buffer.data());
		icu_status = U_ZERO_ERROR;
		ucsdet_close(encoding_detector);
		goto icu_skip;
	}

	ucsdet_setDeclaredEncoding(encoding_detector, "UTF-8", 5, &icu_status);
	if (U_FAILURE(icu_status)) {
		utf_buffer = icu::UnicodeString::fromUTF8(raw_buffer.data());
		icu_status = U_ZERO_ERROR;
		ucsdet_close(encoding_detector);
		goto icu_skip;
	}

	encoding_match = ucsdet_detect(encoding_detector, &icu_status);
	if (U_FAILURE(icu_status)) {
		utf_buffer = icu::UnicodeString::fromUTF8(raw_buffer.data());
		icu_status = U_ZERO_ERROR;
		ucsdet_close(encoding_detector);
		goto icu_skip;
	}

	encoding_name = ucsdet_getName(encoding_match, &icu_status);
	if (U_FAILURE(icu_status)) {
		utf_buffer = icu::UnicodeString::fromUTF8(raw_buffer.data());
		icu_status = U_ZERO_ERROR;
		ucsdet_close(encoding_detector);
		goto icu_skip;
	}

	utf_buffer = icu::UnicodeString(raw_buffer.data(), encoding_name);
	ucsdet_close(encoding_detector);

icu_skip:

	std::vector<icu::UnicodeString> matches{};
	const char*						default_re_str = R"((?:^|\s)([a-zA-Z][a-zA-Z0-9+.-]*)://([^\s]+))";
	icu::RegexPattern*				re			   = icu::RegexPattern::compile(icu::UnicodeString::fromUTF8(params.regex), 0, icu_status);
	if (U_FAILURE(icu_status) || !re) {
		icu_status = U_ZERO_ERROR;
		re		   = icu::RegexPattern::compile(icu::UnicodeString::fromUTF8(default_re_str), 0, icu_status);
	}
	if (U_SUCCESS(icu_status) && re) {
		icu::RegexMatcher* matcher = re->matcher(utf_buffer, icu_status);
		if (U_SUCCESS(icu_status) && matcher) {
			while (matcher->find(icu_status) && U_SUCCESS(icu_status)) {
				icu::UnicodeString fullMatch = matcher->group(0, icu_status);
				if (U_SUCCESS(icu_status)) matches.push_back(fullMatch);
			}
			delete matcher;
		}
		delete re;
	}
	auto strip_fragment = [](icu::UnicodeString s) {
		int32_t pos = s.indexOf(u'#');
		if (pos >= 0) s.truncate(pos);
		return s;
	};

	std::unordered_set<std::string> seen;
	matches.erase(std::remove_if(matches.begin(), matches.end(),
								 [&](const icu::UnicodeString& current) {
									 std::string key;
									 strip_fragment(current).toUTF8String(key);
									 return !seen.insert(key).second;
								 }),
				  matches.end());
	/* removing unnecessary proxies, in accordance with the configuration */
	bool	   whitelist = conf["settings"]["protocols"]["whitelist"].value_or(false);
	const auto protocols = conf["settings"]["protocols"]["list"].as_array();
	if (protocols) {
		matches.erase(std::remove_if(matches.begin(), matches.end(),
									 [&](icu::UnicodeString m) {
										 m.trim();
										 for (const auto& p : *protocols) {
											 const auto str = p.as_string();
											 if (!str) return false; /* allow by default */
											 icu::UnicodeString utf_str = icu::UnicodeString::fromUTF8(str->get() + "://");
											 utf_str.toLower();
											 m.toLower();
											 if (m.startsWith(utf_str)) return !whitelist;
										 }
										 return whitelist;
									 }),
					  matches.end());
	}
	if (matches.empty()) return {};
	size_t nthreads = std::min(params.nthreads, matches.size());

	std::thread				 threads[nthreads];
	json::object			 frags[nthreads];
	std::vector<std::string> bad_proxies_tmp[nthreads];

	std::fill_n(frags, nthreads, EMPTY_XRAY_CONF);
	for (size_t i = 0; i < nthreads; ++i) {
		threads[i] = std::thread([&, i] {
			auto					 start_end_index = subrange((size_t) 0, (size_t) matches.size() - 1, nthreads, i);
			std::vector<std::string> thread_configs;
			for (size_t j = start_end_index.first; j <= start_end_index.second; ++j) {
				std::string u8s;
				matches[j].toUTF8String(u8s);
				thread_configs.push_back(u8s);
			}
			try {
				frags[i] = urls_configgen_core(thread_configs, bad_proxies_tmp[i], msg);
			} catch (...) {
				msg(severity_lvl::error, "unknown error\n");
			}
		});
	}
	for (size_t i = 0; i < nthreads; ++i) threads[i].join();
	for (size_t i = 0; i < nthreads; ++i)
		bad_proxies.insert(bad_proxies.end(), std::make_move_iterator(bad_proxies_tmp[i].begin()),
						   std::make_move_iterator(bad_proxies_tmp[i].end()));
	std::vector<json::object> result	   = refragment_configs(frags, nthreads, params.ppt);
	std::string				  access	   = conf["xray"]["log"]["access"].value_or(PLATFORM_STDOUT);
	std::string				  error		   = conf["xray"]["log"]["error"].value_or(PLATFORM_STDERR);
	std::string				  log_level	   = conf["xray"]["log"]["log_level"].value_or("warning");
	bool					  dns_log	   = conf["xray"]["log"]["dns_log"].value_or(false);
	std::string				  mask_address = conf["xray"]["log"]["mask_address"].value_or("");
	auto					  port		   = params.start_port;
	for (auto& config : result) {
		if (port > (params.start_port + (params.xrays * params.ppt))) port = params.start_port;
		config["log"] = boost::json::object{
			{"access", access}, {"error", error}, {"logLevel", log_level}, {"dnsLog", dns_log}, {"maskAddress", mask_address}};

		for (auto& c : config["inbounds"].as_array()) c.as_object()["port"] = port++;

		auto& inbounds	= config["inbounds"].as_array();
		auto& outbounds = config["outbounds"].as_array();
		auto& rules		= config["routing"].as_object()["rules"].as_array();
		for (size_t i = 0; i < rules.size(); ++i) {
			const std::string id   = std::to_string(i);
			const std::string itag = "in_" + id, otag = "out_" + id;

			inbounds[i].as_object()["tag"]	= itag;
			outbounds[i].as_object()["tag"] = otag;

			rules[i].as_object()["inboundTag"]	= itag;
			rules[i].as_object()["outboundTag"] = otag;
		}
	}
	return result;
}
std::vector<json::object> urls_configgen_auto(const std::vector<std::string>& lists, const toml::table& conf, const v2sort_params& params,
											  std::vector<std::string>&							  bad_proxies,
											  std::function<void(severity_lvl, std::string_view)> msg) {
	std::string data;
	for (const auto& l : lists) {
		if (l.starts_with("file://") || l.find("://") == std::string::npos) {
			try {
				data += ('\n' + read_file(l.starts_with("file://") ? l.c_str() + 7 : l.c_str()));
			} catch (const std::ios_base::failure&) {
				const auto		   e = errno;
				std::ostringstream m;
				m << "failed to retrieve data from '" << l << "': " << std::strerror(e) << '\n';
				msg(severity_lvl::error, m.str());
				continue;
			}
		} else {
			using namespace _net_impl;
			urls::url url;
			if (system::result<urls::url> result = urls::parse_uri(l.data()); !result) {
				std::ostringstream m;
				m << "'" << l << "': " << "invalid url\n";
				msg(severity_lvl::error, m.str());
				continue;
			} else
				url = result.value();
			curl_ptr curl(curl_easy_init(), &curl_easy_cleanup);
			CURL*	 c_ptr = curl.get();
			if (!c_ptr) {
				std::ostringstream m;
				m << "failed to retrieve data from '" << l << "': " << std::make_error_code(std::errc::network_unreachable).message()
				  << '\n';
				msg(severity_lvl::error, m.str());
				continue;
			}
			std::string buffer;

			curl_easy_setopt(c_ptr, CURLOPT_URL, l.data());
			curl_easy_setopt(c_ptr, CURLOPT_NOPROGRESS, 1);

			curl_easy_setopt(c_ptr, CURLOPT_VERBOSE, 1);
			curl_easy_setopt(c_ptr, CURLOPT_DEBUGFUNCTION, debug_callback);
			curl_easy_setopt(c_ptr, CURLOPT_DEBUGDATA, nullptr);

			curl_easy_setopt(c_ptr, CURLOPT_NOSIGNAL, 1);
			curl_easy_setopt(c_ptr, CURLOPT_WRITEFUNCTION, write_callback);
			curl_easy_setopt(c_ptr, CURLOPT_WRITEDATA, &buffer);
			curl_easy_setopt(c_ptr, CURLOPT_HEADERFUNCTION, dummy_callback);
			curl_easy_setopt(c_ptr, CURLOPT_HEADERDATA, nullptr);

			curl_easy_setopt(c_ptr, CURLOPT_FOLLOWLOCATION, 1);
			curl_easy_setopt(c_ptr, CURLOPT_MAXREDIRS, 10);
			curl_easy_setopt(c_ptr, CURLOPT_CONNECTTIMEOUT, params.timeout);
			curl_easy_setopt(c_ptr, CURLOPT_TIMEOUT, params.timeout);
			curl_easy_setopt(c_ptr, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_NONE);

			if (params.ipv4)
				curl_easy_setopt(c_ptr, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
			else if (params.ipv6)
				curl_easy_setopt(c_ptr, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
			else
				curl_easy_setopt(c_ptr, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);
			CURLcode res = curl_easy_perform(c_ptr);
			if (res != CURLE_OK) {
				std::ostringstream m;
				m << "failed to retrieve data from '" << l << "': " << std::make_error_code(res).message() << '\n';
				msg(severity_lvl::error, m.str());
				continue;
			}
			data += ('\n' + buffer);
		}
	}
	if (data.empty()) throw std::runtime_error("nothing to check");

	return urls_configgen_common(data, conf, params, bad_proxies, msg);
}
std::vector<json::object> refragment_configs(json::object* frags, size_t frags_size, size_t ents_per_fragment) {
	std::vector<json::object> out;
	if (ents_per_fragment == 0) return out;

	json::object current = EMPTY_XRAY_CONF;
	size_t		 counter = 0;

	for (size_t i = 0; i < frags_size; ++i) {
		auto&		 inbounds	= frags[i]["inbounds"].as_array();
		auto&		 outbounds	= frags[i]["outbounds"].as_array();
		auto&		 rules		= frags[i]["routing"].as_object().at("rules").as_array();
		const size_t ents_count = std::min({inbounds.size(), outbounds.size(), rules.size()});

		assert(inbounds.size() == outbounds.size() && inbounds.size() == rules.size());

		for (size_t j = 0; j < ents_count; ++j) {
			current["inbounds"].as_array().push_back(inbounds[j]);
			current["outbounds"].as_array().push_back(outbounds[j]);
			current["routing"].as_object()["rules"].as_array().push_back(rules[j]);
			++counter;
			if (counter >= ents_per_fragment) {
				out.push_back(current);
				current = EMPTY_XRAY_CONF;
				counter = 0;
			}
		}
	}

	if (counter > 0) out.push_back(current);
	return out;
}
