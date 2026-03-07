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

struct proxy_report {
	std::string		url;
	geodata			geo;
	connection_info net;
};
enum class out_style { raw, json, human };
struct v2sort_params {
	std::string				   config;
	std::vector<std::string>   list;
	size_t					   nthreads;
	uint16_t				   start_port;
	size_t					   xray_wait;
	size_t					   timeout;
	out_style				   style;
	std::string				   regex;
	std::optional<size_t>	   ppt;
	bool					   verbose;
	bool					   random;
	std::optional<std::string> mmdb_path;
	bool					   ipv4;
	bool					   ipv6;
	std::string				   output;
	std::optional<std::string> xray_conf;
	std::optional<std::string> fragment_format;
	std::optional<std::string> bad;
};
