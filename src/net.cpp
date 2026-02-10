#include "net.hpp"
#include <boost/json.hpp>
#include <boost/log/trivial.hpp>
#include <curl/curl.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <syncstream>

using namespace boost;
namespace {
size_t dummy_callback(char*, size_t size, size_t nmemb, void*) {
	return size * nmemb;
}
size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
	std::string* response = static_cast<std::string*>(userdata);
	response->append(ptr, size * nmemb);
	return size * nmemb;
}

size_t debug_callback(CURL*, curl_infotype type, char* data, size_t size, void*) {
	if (type == CURLINFO_TEXT) BOOST_LOG_TRIVIAL(debug) << std::string(data, size);
	return 0;
}
} // namespace
using curl_ptr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

connection_info httpcheck(uint16_t proxy_port, const std::string& url, uint32_t timeout, int flags) {
	if ((flags & NET_IPV4_ONLY) && (flags & NET_IPV6_ONLY)) {
		BOOST_LOG_TRIVIAL(debug) << "NET_IPV4_ONLY and NET_IPV6_ONLY are mutually exclusive\n";
		return {};
	}
	curl_ptr curl(curl_easy_init(), &curl_easy_cleanup);
	CURL*	 c_ptr = curl.get();

	if (!c_ptr) {
		BOOST_LOG_TRIVIAL(fatal) << "Failed to create curl object\n";
		return {};
	}

	curl_easy_setopt(c_ptr, CURLOPT_URL, url.c_str());

	/* proxy */
	std::string proxy = "127.0.0.1:" + std::to_string(proxy_port);
	curl_easy_setopt(c_ptr, CURLOPT_PROXY, proxy.c_str());
	curl_easy_setopt(c_ptr, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);

	/* curl output */
	curl_easy_setopt(c_ptr, CURLOPT_NOPROGRESS, 1);

	curl_easy_setopt(c_ptr, CURLOPT_VERBOSE, 1);
	curl_easy_setopt(c_ptr, CURLOPT_DEBUGFUNCTION, debug_callback);
	curl_easy_setopt(c_ptr, CURLOPT_DEBUGDATA, nullptr);

	curl_easy_setopt(c_ptr, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(c_ptr, CURLOPT_WRITEFUNCTION, dummy_callback);
	curl_easy_setopt(c_ptr, CURLOPT_WRITEDATA, nullptr);
	curl_easy_setopt(c_ptr, CURLOPT_HEADERFUNCTION, dummy_callback);
	curl_easy_setopt(c_ptr, CURLOPT_HEADERDATA, nullptr);

	curl_easy_setopt(c_ptr, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(c_ptr, CURLOPT_MAXREDIRS, 10);
	curl_easy_setopt(c_ptr, CURLOPT_CONNECTTIMEOUT, timeout);
	curl_easy_setopt(c_ptr, CURLOPT_TIMEOUT, timeout);
	curl_easy_setopt(c_ptr, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_NONE);

	if (flags & NET_IPV4_ONLY)
		curl_easy_setopt(c_ptr, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	else if (flags & NET_IPV6_ONLY)
		curl_easy_setopt(c_ptr, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
	else
		curl_easy_setopt(c_ptr, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);

	curl_easy_setopt(
		c_ptr, CURLOPT_USERAGENT,
		R"(Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/116.0.5845.140 Safari/537.36)");
	/*curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1);
	curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);*/

	CURLcode res = curl_easy_perform(c_ptr);
	if (res != CURLE_OK) {
		BOOST_LOG_TRIVIAL(error) << url << ": " << curl_easy_strerror(res) << '\n';
		return {};
	}

	connection_info ni{};
	curl_easy_getinfo(c_ptr, CURLINFO_RESPONSE_CODE, &ni.http_code);
	curl_easy_getinfo(c_ptr, CURLINFO_NAMELOOKUP_TIME, &ni.t_dns);
	curl_easy_getinfo(c_ptr, CURLINFO_CONNECT_TIME, &ni.t_connect);
	curl_easy_getinfo(c_ptr, CURLINFO_APPCONNECT_TIME, &ni.t_tls);
	curl_easy_getinfo(c_ptr, CURLINFO_STARTTRANSFER_TIME, &ni.t_ttfb);
	curl_easy_getinfo(c_ptr, CURLINFO_TOTAL_TIME, &ni.t_total);
	curl_easy_getinfo(c_ptr, CURLINFO_SPEED_DOWNLOAD_T, &ni.speed);
	curl_easy_getinfo(c_ptr, CURLINFO_SIZE_DOWNLOAD_T, &ni.size);

	return ni;
}

ipinfo_proxy ipinfo(uint16_t proxy_port, uint32_t timeout, int flags) {
	if ((flags & NET_IPV4_ONLY) && (flags & NET_IPV6_ONLY)) {
		BOOST_LOG_TRIVIAL(debug) << "NET_IPV4_ONLY and NET_IPV6_ONLY are mutually exclusive\n";
		return {};
	}
	curl_ptr curl(curl_easy_init(), &curl_easy_cleanup);
	CURL*	 c_ptr = curl.get();

	if (!c_ptr) {
		BOOST_LOG_TRIVIAL(fatal) << "Failed to create curl object" << '\n';
		return {};
	}

	curl_easy_setopt(c_ptr, CURLOPT_URL, "https://ipinfo.io/json");

	/* proxy */
	const std::string proxy = "127.0.0.1:" + std::to_string(proxy_port);
	curl_easy_setopt(c_ptr, CURLOPT_PROXY, proxy.c_str());
	curl_easy_setopt(c_ptr, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);

	std::string raw_json;

	/* curl output */
	curl_easy_setopt(c_ptr, CURLOPT_NOPROGRESS, 1);

	curl_easy_setopt(c_ptr, CURLOPT_VERBOSE, 1);
	curl_easy_setopt(c_ptr, CURLOPT_DEBUGFUNCTION, debug_callback);
	curl_easy_setopt(c_ptr, CURLOPT_DEBUGDATA, nullptr);

	curl_easy_setopt(c_ptr, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(c_ptr, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(c_ptr, CURLOPT_WRITEDATA, &raw_json);
	curl_easy_setopt(c_ptr, CURLOPT_HEADERFUNCTION, dummy_callback);
	curl_easy_setopt(c_ptr, CURLOPT_HEADERDATA, nullptr);

	curl_easy_setopt(c_ptr, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(c_ptr, CURLOPT_MAXREDIRS, 10);
	curl_easy_setopt(c_ptr, CURLOPT_CONNECTTIMEOUT, timeout);
	curl_easy_setopt(c_ptr, CURLOPT_TIMEOUT, timeout);
	curl_easy_setopt(c_ptr, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_NONE);

	if (flags & NET_IPV4_ONLY)
		curl_easy_setopt(c_ptr, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	else if (flags & NET_IPV6_ONLY)
		curl_easy_setopt(c_ptr, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
	else
		curl_easy_setopt(c_ptr, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);

	curl_easy_setopt(
		c_ptr, CURLOPT_USERAGENT,
		R"(Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/116.0.5845.140 Safari/537.36)");
	/*curl_easy_setopt(curl.get(), CURLOPT_FRESH_CONNECT, 1);
	curl_easy_setopt(curl.get(), CURLOPT_FORBID_REUSE, 1);
	curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 0);
	curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 0);*/

	CURLcode res = curl_easy_perform(c_ptr);
	if (res != CURLE_OK) {
		BOOST_LOG_TRIVIAL(error) << "https://ipinfo.io/json: " << curl_easy_strerror(res) << '\n';
		return {};
	}

	try {
		json::object j = json::parse(raw_json).as_object();
		ipinfo_proxy metric{};
		metric.ip		= j["ip"].as_string();
		metric.country	= j["country"].as_string();
		metric.region	= j["region"].as_string();
		metric.city		= j["city"].as_string();
		metric.timezone = j["timezone"].as_string();
		metric.org		= j["org"].as_string();
		metric.postal	= j["postal"].as_string();
		metric.loc		= j["loc"].as_string();
		return metric;
	} catch (const json::system_error& e) {
		BOOST_LOG_TRIVIAL(error) << "https://ipinfo.io: " << e.code().category().name() << ": " << e.code().message() << '\n'
								 << e.code().category().name() << ": " << "what(): " << e.what() << '\n';
		return {};
	}
}
