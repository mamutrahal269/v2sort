#pragma once

#include <boost/outcome.hpp>
#include <cstdint>
#include <curl/curl.h>
#include <iostream>
#include <string>
#include <system_error>

constexpr auto NET_IPV4_ONLY = 1 << 0, NET_IPV6_ONLY = NET_IPV4_ONLY << 1;
struct connection_info {
	long	   http_code;
	double	   t_dns;
	double	   t_connect;
	double	   t_tls;
	double	   t_ttfb;
	double	   t_total;
	curl_off_t speed;
	curl_off_t size;
};
class _curl_category : public std::error_category {
  public:
	const char* name() const noexcept override { return "curl"; }
	std::string message(int ev) const override { return curl_easy_strerror(static_cast<CURLcode>(ev)); }
};
inline const _curl_category& curl_category() {
	static _curl_category cat;
	return cat;
}
namespace std {
template <> struct is_error_code_enum<CURLcode> : true_type {};
inline std::error_code make_error_code(CURLcode e) {
	return {static_cast<int>(e), curl_category()};
}
} // namespace std

namespace _net_impl {
using curl_ptr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
size_t dummy_callback(char*, size_t size, size_t nmemb, void*);
size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
size_t debug_callback(CURL*, curl_infotype type, char* data, size_t size, void*);
} // namespace _net_impl
BOOST_OUTCOME_V2_NAMESPACE::result<connection_info, std::error_code> httpcheck(uint16_t proxy_port, const std::string& url,
																			   uint32_t timeout, int flags);
