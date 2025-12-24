#include "net.hpp"
#include <iostream>
#include <mutex>
#include <curl/curl.h>
#include <boost/json.hpp>

extern std::mutex stdout_mtx;
using namespace boost;
namespace {
	size_t dummy_callback(char*, size_t size, size_t nmemb, void*) { return size * nmemb; }
	size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
		std::string *response = static_cast<std::string*>(userdata);
		response->append(ptr, size * nmemb);
		return size * nmemb;
	}
	class curl_guard {
		CURL* curl_;
	public:
		explicit curl_guard(CURL* curl) : curl_(curl) {}
		~curl_guard() {
			if (curl_) curl_easy_cleanup(curl_);
		}

		curl_guard(const curl_guard&) = delete;
		curl_guard& operator=(const curl_guard&) = delete;
	};
}
conmetric httpcheck(uint16_t proxy_port, const std::string &url, uint32_t timeout, std::ostream& err_out) {
	CURL *curl = curl_easy_init();
	curl_guard guard(curl);
	
	if (!curl) {
		if(&err_out == &std::cerr) {
			std::lock_guard<std::mutex> lk(stdout_mtx);
			std::cerr << "Failed to create curl object" << '\n';
		} else err_out << "Failed to create curl object" << '\n';
		return {};
	}

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	
	/* proxy */
	std::string proxy = "127.0.0.1:" + std::to_string(proxy_port);
	curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
	curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);

	/* curl output */
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, dummy_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, nullptr);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, dummy_callback);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, nullptr);
	curl_easy_setopt(curl, CURLOPT_STDERR, nullptr);
	
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_NONE);
	
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		if(&err_out == &std::cerr) {
			std::lock_guard<std::mutex> lk(stdout_mtx);
			std::cerr << url << ": " << curl_easy_strerror(res) << '\n';
		} else err_out << url << ": " << curl_easy_strerror(res) << '\n';
		return {};
	}

	conmetric ni{};
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ni.http_code);
	curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &ni.t_dns);
	curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &ni.t_connect);
	curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &ni.t_tls);
	curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &ni.t_ttfb);
	curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &ni.t_total);
	curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD, &ni.speed);
	curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &ni.size);
	
	return ni;
}

geometric geometric_with_proxy(uint16_t proxy_port, uint32_t timeout, std::ostream& err_out) {
	CURL *curl = curl_easy_init();
	curl_guard guard(curl);
	
	if (!curl) {
		if(&err_out == &std::cerr) {
			std::lock_guard<std::mutex> lk(stdout_mtx);
			std::cerr << "Failed to create curl object" << '\n';
		} else err_out << "Failed to create curl object" << '\n';
		return {};
	}
	
	curl_easy_setopt(curl, CURLOPT_URL, "https://ipinfo.io/json");
	
	/* proxy */
	const std::string proxy = "127.0.0.1:" + std::to_string(proxy_port);
	curl_easy_setopt(curl, CURLOPT_PROXY, proxy.c_str());
	curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);
	
	std::string raw_json;
	
	/* curl output */
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 0);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &raw_json);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, dummy_callback);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, nullptr);
	curl_easy_setopt(curl, CURLOPT_STDERR, nullptr);
	
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
	curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_NONE);
	
	curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	
	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		if(&err_out == &std::cerr) {
			std::lock_guard<std::mutex> lk(stdout_mtx);
			std::cerr << "https://ipinfo.io: " << curl_easy_strerror(res) << '\n';
		} else err_out << "https://ipinfo.io: " << curl_easy_strerror(res) << '\n';
		return {};
	}

	try {
		json::object j = json::parse(raw_json).as_object();
		geometric metric{};
		metric.ip = j["ip"].as_string();
		metric.country = j["country"].as_string();
		metric.region = j["region"].as_string();
		metric.city = j["city"].as_string();
		metric.timezone = j["timezone"].as_string();
		metric.org = j["org"].as_string();
		metric.postal = j["postal"].as_string();
		metric.loc = j["loc"].as_string();
		return metric;
	} catch(const json::system_error& e) {
		if(&err_out == &std::cerr) {
			std::lock_guard<std::mutex> lk(stdout_mtx);
			std::cerr << "https://ipinfo.io: " << e.code().category().name() << ": " << e.code().message() << '\n'
			<< e.code().category().name() << ": " << "what(): " << e.what() << '\n';
		} else err_out << "https://ipinfo.io: " << e.code().category().name() << ": " << e.code().message() << '\n'
			<< e.code().category().name() << ": " << "what(): " << e.what() << '\n';
		return {};
	}
}
