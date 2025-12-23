#include "mkproto.hpp"
#include "configgen.hpp"
#include <mutex>
#include <atomic>
#include <vector>
#include <utility>
#include <cstdint>
#include <iostream>
#include <sstream>
using namespace boost;
extern std::mutex stdout_mtx;

json::object configgen(const std::vector<std::pair<std::string, uint16_t>>& list, const uint16_t start_port) {
	std::ostringstream stderr_buf;
	json::object out;
	out["inbounds"] = json::array{};
	out["outbounds"] = json::array{};
	out["routing"] = json::object{
		{"domainStrategy", "AsIs"}
	};
	out["routing"].as_object()["rules"] = json::array{};
	for(const auto& [conf, port] : list) {
		const auto id = std::to_string(port - start_port + 1);
		try {
			out["outbounds"].as_array().push_back((
				conf.starts_with("vless://") ?
					mkvless :
				conf.starts_with("vmess://") ?
					mkvmess :
				conf.starts_with("ss://") ?
					mkss :
				conf.starts_with("trojan://") ?
					mktrojan :
				conf.starts_with("http://") ?
					mkhttp :
					throw inval_proto("unsupported protocol"))(conf, "out_" + id));
		} catch(const std::exception& e) {
			stderr_buf << "failed to create JSON for " << conf << '\n' << "what(): " << e.what() << std::endl;
			continue;
		}
		out["inbounds"].as_array().push_back(
			json::object {
				{"tag", "in_" + id},
				{"listen", "127.0.0.1"},
				{"protocol", "socks"},
				{"port", port},
				{"settings", json::object {
						{"auth", "noauth"},
						{"udp", true}
					}
				}
			}
		);
		out["routing"].as_object()["rules"].as_array().push_back(
			json::object {
				{"inboundTag", json::array{"in_" +  id}},
				{"outboundTag", "out_" +  id},
			}
		);
	}
	std::lock_guard<std::mutex> lk(stdout_mtx);
	std::clog << stderr_buf.str();
	return out;
}
