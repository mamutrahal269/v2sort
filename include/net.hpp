#include <cstdint>
#include <curl/curl.h>
#include <iostream>
#include <string>

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
struct ipinfo_proxy {
	std::string ip, country, region, city, timezone, loc, org, postal;
};
connection_info httpcheck(uint16_t proxy_port, const std::string& url, uint32_t timeout, int flags);
ipinfo_proxy	ipinfo(uint16_t proxy_port, uint32_t timeout, int flags);
