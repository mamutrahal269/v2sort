#include "configgen.hpp"
#include "net.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <utility>
#include <thread>
#include <mutex>
#include <boost/process.hpp>
#include <chrono>
#include <curl/curl.h>

using namespace boost;
std::mutex stdout_mtx;

int main() {
	std::ifstream lists("lists");
	std::ofstream out_conf("config.json", std::ios::trunc);
	
	std::vector<std::pair<std::string, uint16_t>> configs[4];
	
	configs[0].push_back({"", 10808});
	configs[1].push_back({"", 10808});
	configs[2].push_back({"", 10808});
	configs[3].push_back({"", 10808});
	
	
	json::object results[4], final;
	std::string line;
	uint32_t start_port = 10809, port = start_port;
	size_t idx = 0;

	while(getline(lists, line) && port <= 0xFFFF)
		configs[idx++ % 4].push_back({line, static_cast<uint16_t>(port++)});
	
	/* initialize results and final with default values */
	final["inbounds"] = json::array{};
	final["outbounds"] = json::array{};
	final["routing"] = json::object{
		{"domainStrategy", "AsIs"},
		{"rules", json::array{}}
	};
	for(int i = 0; i < 4; ++i)
		results[i] = final;
	std::thread threads[4];

	for(int i = 0; i < 4; ++i) {
		threads[i] = std::thread([start_port, i, &configs, &results] {
			try {
				results[i] = configgen(configs[i], start_port);
			} catch(const std::exception& e) {
				std::lock_guard<std::mutex> lk(stdout_mtx);
				std::cerr << "std::exception: " << e.what() << '\n';
			} catch(...) {
				std::lock_guard<std::mutex> lk(stdout_mtx);
				std::cerr << "Unknown error\n";
			}
		});
	}

	for(int i = 0; i < 4; ++i)
		threads[i].join();
	
	/* merge */
	for(int i = 0; i < 4; ++i) {
		/* inbounds */
		for(const auto& val : results[i]["inbounds"].as_array())
			final["inbounds"].as_array().push_back(val);
		
		/* outbounds */
		for(const auto& val : results[i]["outbounds"].as_array())
			final["outbounds"].as_array().push_back(val);
		
		/* routingRules */
		for(const auto& val : results[i]["routing"].as_object()["rules"].as_array())
			final["routing"].as_object()["rules"].as_array().push_back(val);
	}

	out_conf << final;
	out_conf.flush();
	out_conf.close();
	
	process::child c("/usr/local/bin/xray", "run", "-c", "config.json");
	std::this_thread::sleep_for(std::chrono::seconds(5));
	
	curl_global_init(CURL_GLOBAL_DEFAULT);
	
	for(int i = 0; i < 4; ++i) {
		threads[i] = std::thread([&configs, i] {
			std::ostringstream stdout_buf;
			for(const auto&[_, port] : configs[i]) {
				const auto info = httpcheck(port, "http://ya.ru", 5);
				
				if(info.http_code != 0) {
					stdout_buf << "HTTP Code: " << info.http_code << "\n";
					stdout_buf << "DNS lookup: " << info.t_dns*1000 << " ms\n";
					stdout_buf << "Connect: " << info.t_connect*1000 << " ms\n";
					stdout_buf << "TLS handshake: " << info.t_tls*1000 << " ms\n";
					stdout_buf << "TTFB: " << info.t_ttfb*1000 << " ms\n";
					stdout_buf << "Total time: " << info.t_total*1000 << " ms\n";
					stdout_buf << "Downloaded: " << info.size/1024.0 << " KB\n";
					stdout_buf << "Speed: " << info.speed/1024.0 << " KB/s\n";
				}
			}
			std::lock_guard<std::mutex> lk(stdout_mtx);
			std::cout << stdout_buf.str();
		});
	}
	for(int i = 0; i < 4; ++i)
		threads[i].join();
	curl_global_cleanup();
	return 0;
}
