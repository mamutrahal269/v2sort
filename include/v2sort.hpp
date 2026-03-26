#pragma once

#include "geodata.hpp"
#include "net.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32

#define PLATFORM_STDOUT "CONOUT$"
#define PLATFORM_STDERR "CONERR$"
#define PLATFORM_NULL "NUL"

#else

#define PLATFORM_STDOUT "/dev/stdout"
#define PLATFORM_STDERR "/dev/stderr"
#define PLATFORM_NULL "/dev/null"

#endif

struct alignas(64) proxy_report {
	std::string					 url;
	geodata						 geo;
	std::vector<connection_info> net;
	std::optional<size_t>		 speed;
};
enum class severity_lvl { trace, info, debug, warning, error, fatal };
enum class out_style { raw, json, human };
enum class geo_service {
#ifdef MMDB_SUPPORTED
	mmdb,
#endif
	ipinfo,
	cdn_cgi
};
struct v2sort_params {
	std::string				   config;
	std::vector<std::string>   list;
	size_t					   nthreads;
	uint16_t				   start_port;
	size_t					   xray_wait;
	size_t					   timeout;
	out_style				   style;
	std::string				   regex;
	size_t					   ppt;
	bool					   verbose;
	bool					   random;
	std::optional<std::string> mmdb_path;
	bool					   ipv4;
	bool					   ipv6;
	std::string				   output;
	std::optional<std::string> xray_conf;
	std::optional<std::string> fragment_format;
	std::optional<std::string> bad;
	bool					   speedtest;
	bool					   trunc_report;
	bool					   trunc_bad;
	std::optional<geo_service> service;
	size_t					   xrays;
};
