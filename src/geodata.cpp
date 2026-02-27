#include "geodata.hpp"
#include "net.hpp"
#include <boost/json.hpp>
#include <boost/outcome.hpp>
#include <format>
#include <memory>
#include <system_error>

namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;
using namespace boost;

#ifdef MMDB_SUPPORTED
#include <maxminddb.h>

outcome::result<geodata, std::error_code> mmdb_geodata(MMDB_s& mmdb, std::string_view ip) {
	int					 gai_err, mmdb_err;
	MMDB_lookup_result_s result = MMDB_lookup_string(&mmdb, ip.data(), &gai_err, &mmdb_err);
	if (gai_err) return outcome::failure(std::error_code(gai_err, gai_category()));
	if (mmdb_err) return outcome::failure(std::error_code(mmdb_err, maxminddb_category()));
	if (!result.found_entry) return outcome::failure(std::make_error_code(std::errc::invalid_argument));

	auto get_entry = [&result]<typename... T>(T... entry_path) -> std::string {
		MMDB_entry_data_s entry_data;
		int				  status = MMDB_get_value(&result.entry, &entry_data, entry_path..., nullptr);
		if (status == MMDB_SUCCESS && entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING)
			return std::string(entry_data.utf8_string, entry_data.data_size);
		return {};
	};
	geodata geo;
	geo.ip		= ip;
	geo.country = get_entry("country", "iso_code");
	geo.region	= get_entry("subdivisions", "0", "iso_code");
	geo.city	= get_entry("city", "names", "en");
	return outcome::success(geo);
}
#endif
outcome::result<geodata, std::error_code> ipinfo_geodata(uint16_t proxy_port, uint32_t timeout, int flags) {
	using namespace _net_impl;
	assert((flags & (NET_IPV4_ONLY | NET_IPV6_ONLY)) != (NET_IPV4_ONLY | NET_IPV6_ONLY));

	curl_ptr curl(curl_easy_init(), &curl_easy_cleanup);
	CURL*	 c_ptr = curl.get();

	if (!c_ptr) return outcome::failure(std::make_error_code(std::errc::network_unreachable));

	curl_easy_setopt(c_ptr, CURLOPT_URL, "https://ipinfo.io/json");

	/* proxy */
	const std::string proxy = "127.0.0.1:" + std::to_string(proxy_port);
	curl_easy_setopt(c_ptr, CURLOPT_PROXY, proxy.c_str());
	curl_easy_setopt(c_ptr, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME);

	std::string raw_json;

	/* curl output */
	curl_easy_setopt(c_ptr, CURLOPT_NOPROGRESS, 1);

	curl_easy_setopt(c_ptr, CURLOPT_VERBOSE, 1);
	curl_easy_setopt(c_ptr, CURLOPT_DEBUGFUNCTION, debug_callback);
	curl_easy_setopt(c_ptr, CURLOPT_DEBUGDATA, nullptr);

	curl_easy_setopt(c_ptr, CURLOPT_NOSIGNAL, 1);
	curl_easy_setopt(c_ptr, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(c_ptr, CURLOPT_WRITEDATA, &raw_json);
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

	try {
		json::object j = json::parse(raw_json).as_object();
		geodata		 geo{};
		geo.ip		= j["ip"].as_string();
		geo.country = j["country"].as_string();
		geo.region	= j["region"].as_string();
		geo.city	= j["city"].as_string();
		return outcome::success(geo);
	} catch (const json::system_error& e) {
		return outcome::failure(std::error_code(e.code()));
	}
}
