#include "configgen.hpp"
#include "mkproto.hpp"
#include <atomic>
#include <boost/log/trivial.hpp>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <regex>
#include <string_view>
#include <syncstream>
#include <utility>
#include <vector>
using namespace boost;

json::object configgen(const std::vector<std::string>& proxies) {
	json::object out = EMPTY_XRAY_CONF;
	for (const auto& conf : proxies) {
		try {
			out["outbounds"].as_array().push_back((conf.starts_with("vless://")	   ? mkvless
												   : conf.starts_with("vmess://")  ? mkvmess
												   : conf.starts_with("ss://")	   ? mkss
												   : conf.starts_with("trojan://") ? mktrojan
												   : conf.starts_with("http://")   ? mkhttp
																				   : throw inval_proto("unsupported protocol"))(conf, ""));
		} catch (const std::exception& e) {
			BOOST_LOG_TRIVIAL(warning) << "failed to create entry for " << conf << ". Problem: " << e.what() << '\n';
			continue;
		}
		out["inbounds"].as_array().push_back(json::object{{"tag", ""},
														  {"src", conf},
														  {"listen", "127.0.0.1"},
														  {"protocol", "socks"},
														  {"port", 0},
														  {"settings", json::object{{"auth", "noauth"}, {"udp", true}}}});
		out["routing"].as_object()["rules"].as_array().push_back(json::object{
			{"inboundTag", json::array{""}},
			{"outboundTag", ""},
		});
	}
	return out;
}
void fill_tags(json::object& config) {
	auto& inbounds	= config["inbounds"].as_array();
	auto& outbounds = config["outbounds"].as_array();
	auto& rules		= config["routing"].as_object()["rules"].as_array();

	for (size_t i = 0; i < rules.size(); ++i) {
		const std::string id   = std::to_string(i);
		const std::string itag = "in_" + id, otag = "out_" + id;

		inbounds[i].as_object()["tag"]	= itag;
		outbounds[i].as_object()["tag"] = otag;

		rules[i].as_object()["inboundTag"]	= itag;
		rules[i].as_object()["outboundTag"] = otag;
	}
}
std::vector<std::smatch> extract_proxies(const char* custom_pattern, const std::string& buffer) {
	std::regex	re;
	const char* re_str = R"((?:^|\s)([a-zA-Z][a-zA-Z0-9+.-]*)://([^\s]+))";
	try {
		re = std::regex(custom_pattern ? custom_pattern : re_str);
	} catch (const std::regex_error&) {
		BOOST_LOG_TRIVIAL(error) << custom_pattern << ": invalid regex\n";
		re = std::regex(re_str);
	}
	std::vector<std::smatch> matches;
	for (auto it = std::sregex_iterator(buffer.begin(), buffer.end(), re); it != std::sregex_iterator(); ++it) {
		if ((*it)[0].matched) {
			BOOST_LOG_TRIVIAL(debug) << it->str() << '\n';
			matches.push_back(*it);
		}
	}
	return matches;
}
std::vector<json::object> refragment_configs(json::object* frags, size_t frags_size, size_t ents_per_fragment) {
	std::vector<json::object> out;
	if (ents_per_fragment == 0) return out;

	json::object current = EMPTY_XRAY_CONF;
	size_t		 counter = 0;

	for (size_t i = 0; i < frags_size; ++i) {
		auto& inbounds	= frags[i]["inbounds"].as_array();
		auto& outbounds = frags[i]["outbounds"].as_array();
		auto& rules		= frags[i]["routing"].as_object().at("rules").as_array();

		for (auto& val : inbounds) {
			current["inbounds"].as_array().push_back(val);
			++counter;
			if (counter >= ents_per_fragment) {
				out.push_back(current);
				current = EMPTY_XRAY_CONF;
				counter = 0;
			}
		}
		for (auto& val : outbounds) {
			current["outbounds"].as_array().push_back(val);
		}
		for (auto& val : rules) {
			current["routing"].as_object()["rules"].as_array().push_back(val);
		}
	}

	if (counter > 0) out.push_back(current);
	return out;
}
