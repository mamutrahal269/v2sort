#pragma once

#include <boost/outcome.hpp>
#include <string>
#include <string_view>

struct geodata {
	std::string ip;
	std::string country;
	std::string city;
	std::string region;
};
#ifndef _WIN32
#define MMDB_SUPPORTED
#include <maxminddb.h>
class _maxminddb_category : public std::error_category {
  public:
	const char* name() const noexcept override { return "maxminddb"; }
	std::string message(int ev) const override { return MMDB_strerror(ev); }
};
inline const _maxminddb_category& maxminddb_category() noexcept {
	static _maxminddb_category cat;
	return cat;
}
class _gai_category : public std::error_category {
  public:
	const char* name() const noexcept override { return "gai"; }
	std::string message(int ev) const override { return gai_strerror(ev); }
};
inline const _gai_category& gai_category() noexcept {
	static _gai_category cat;
	return cat;
}
BOOST_OUTCOME_V2_NAMESPACE::result<geodata, std::error_code> mmdb_geodata(MMDB_s& mmdb, std::string_view ip);
#endif
BOOST_OUTCOME_V2_NAMESPACE::result<geodata, std::error_code> ipinfo_geodata(uint16_t proxy_port, uint32_t timeout, int flags);
