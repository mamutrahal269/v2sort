#include <boost/json.hpp>
#include <cstdint>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#define EMPTY_XRAY_CONF                                                                                                                    \
	json::object {                                                                                                                         \
		{"inbounds", json::array()}, {"outbounds", json::array()}, {                                                                       \
			"routing", json::object {                                                                                                      \
				{                                                                                                                          \
					"rules", json::array()                                                                                                 \
				}                                                                                                                          \
			}                                                                                                                              \
		}                                                                                                                                  \
	}

boost::json::object configgen(const std::vector<std::string>& proxies);
inline void			fill_ports(boost::json::object& config, uint16_t start_port) {
	for (auto& i : config["inbounds"].as_array()) i.as_object()["port"] = start_port++;
}
void		fill_tags(boost::json::object& config);
inline void add_log_obj(boost::json::object& config, std::string_view access, std::string_view error, std::string_view logLevel,
						bool dnsLog, std::string_view maskAddress) {
	config["log"] =
		boost::json::object{{"access", access}, {"error", error}, {"logLevel", logLevel}, {"dnsLog", dnsLog}, {"maskAddress", maskAddress}};
}
std::vector<std::smatch>		 extract_proxies(const char* custom_pattern, const std::string& buffer);
std::vector<boost::json::object> refragment_configs(boost::json::object* frags, size_t frags_size, size_t ents_per_fragment);
