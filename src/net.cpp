#include "net.hpp"
#include <boost/json.hpp>
#include <boost/log/trivial.hpp>
#include <curl/curl.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <syncstream>

using namespace boost;
using namespace _net_impl;
namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;

size_t _net_impl::dummy_callback(char*, size_t size, size_t nmemb, void*) {
	return size * nmemb;
}
size_t _net_impl::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
	std::string* response = static_cast<std::string*>(userdata);
	response->append(ptr, size * nmemb);
	return size * nmemb;
}

size_t _net_impl::debug_callback(CURL*, curl_infotype type, char* data, size_t size, void*) {
	if (type == CURLINFO_TEXT) BOOST_LOG_TRIVIAL(debug) << std::string(data, size);
	return 0;
}

outcome::result<connection_info, std::error_code> httpcheck(uint16_t proxy_port, const std::string& url, uint32_t timeout, int flags) {
	assert((flags & (NET_IPV4_ONLY | NET_IPV4_ONLY)) != (NET_IPV4_ONLY | NET_IPV4_ONLY));

	curl_ptr curl(curl_easy_init(), &curl_easy_cleanup);
	CURL*	 c_ptr = curl.get();

	if (!c_ptr) return outcome::failure(std::make_error_code(std::errc::network_unreachable));

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

	CURLcode res = curl_easy_perform(c_ptr);
	if (res != CURLE_OK) return outcome::failure(std::make_error_code(res));

	connection_info ni{};
	curl_easy_getinfo(c_ptr, CURLINFO_RESPONSE_CODE, &ni.http_code);
	curl_easy_getinfo(c_ptr, CURLINFO_NAMELOOKUP_TIME, &ni.t_dns);
	curl_easy_getinfo(c_ptr, CURLINFO_CONNECT_TIME, &ni.t_connect);
	curl_easy_getinfo(c_ptr, CURLINFO_APPCONNECT_TIME, &ni.t_tls);
	curl_easy_getinfo(c_ptr, CURLINFO_STARTTRANSFER_TIME, &ni.t_ttfb);
	curl_easy_getinfo(c_ptr, CURLINFO_TOTAL_TIME, &ni.t_total);
	curl_easy_getinfo(c_ptr, CURLINFO_SPEED_DOWNLOAD_T, &ni.speed);
	curl_easy_getinfo(c_ptr, CURLINFO_SIZE_DOWNLOAD_T, &ni.size);

	return outcome::success(ni);
}
