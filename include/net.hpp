#include <cstdint>
#include <string>
#include <iostream>

struct conmetric {
	long http_code;
	double t_dns;
	double t_connect;
	double t_tls;
	double t_ttfb;
	double t_total;
	double speed;
	double size;
};
struct geometric {
	std::string ip, country, region, city, timezone, loc, org, postal;
};
conmetric httpcheck(uint16_t proxy_port, const std::string &url, uint32_t timeout, std::ostream& out);
geometric geometric_with_proxy(uint16_t proxy_port, uint32_t timeout, std::ostream& err_out);
