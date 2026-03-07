#pragma once

#include "v2sort.hpp"
#include <boost/json.hpp>
#include <cstdint>
#include <regex>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
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

boost::json::object				 urls_configgen_core(const std::vector<std::string>& proxies, std::vector<std::string>& bad_proxies);
std::vector<boost::json::object> refragment_configs(boost::json::object* frags, size_t frags_size, size_t ents_per_fragment);
std::vector<boost::json::object> urls_configgen_common(const std::string& buffer, const toml::table& conf, const v2sort_params& params,
													   std::vector<std::string>& bad_proxies);
std::vector<boost::json::object> urls_configgen_file(std::string_view path, const toml::table& conf, const v2sort_params& params,
													 std::vector<std::string>& bad_proxies);
std::vector<boost::json::object> urls_configgen_url(std::string_view url_str, const toml::table& conf, const v2sort_params& params,
													std::vector<std::string>& bad_proxies);
