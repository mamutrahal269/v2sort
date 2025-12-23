#include "net.hpp"
#include <iostream>
#include <mutex>
#include <curl/curl.h>
extern std::mutex stdout_mtx;

coninfo httpcheck(uint16_t proxy_port, const std::string &url, uint32_t timeout) {
	CURL *curl = curl_easy_init();
	if (!curl) return {};

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	
	/* proxy */
	std::string proxy = "127.0.0.1:" + std::to_string(proxy_port);
	curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
	curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);

	/* curl output */
	auto dummy_callback = +[](char*, size_t size, size_t nmemb, void*) -> size_t { return size * nmemb; };
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dummy_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, nullptr);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, dummy_callback);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, nullptr);
	curl_easy_setopt(curl, CURLOPT_STDERR, nullptr);
	
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_NONE);
	
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);



	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		std::lock_guard<std::mutex> lk(stdout_mtx);
		std::cerr << proxy_port << ": " << curl_easy_strerror(res) << '\n';
		curl_easy_cleanup(curl);
		return {};
	}

	coninfo ni{};
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ni.http_code);
	curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &ni.t_dns);
	curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &ni.t_connect);
	curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &ni.t_tls);
	curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &ni.t_ttfb);
	curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &ni.t_total);
	curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD, &ni.speed);
	curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &ni.size);

	curl_easy_cleanup(curl);
	return ni;
}
	
