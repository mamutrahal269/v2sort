#include "configgen.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <utility>
#include <thread>
#include <mutex>

using namespace boost;
std::mutex stderr_mtx;

int main() {
	std::ifstream lists("lists");

	std::vector<std::pair<std::string, uint16_t>> configs[4];
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
				std::lock_guard<std::mutex> lk(stderr_mtx);
				std::cerr << "std::exception: " << e.what() << '\n';
			} catch(...) {
				std::lock_guard<std::mutex> lk(stderr_mtx);
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

	std::cout << final << std::endl;
	return 0;
}
