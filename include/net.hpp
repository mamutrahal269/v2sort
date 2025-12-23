#include <cstdint>
#include <string>

struct coninfo {
	long http_code;
	double t_dns;
	double t_connect;
	double t_tls;
	double t_ttfb;
	double t_total;
	double speed;
	double size;
};
coninfo httpcheck(uint16_t proxy_port, const std::string &url, uint32_t timeout);
