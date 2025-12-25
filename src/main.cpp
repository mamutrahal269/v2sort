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
#include <random>

using namespace boost;
std::mutex stdout_mtx;

int main(int argc, char* argv[]) {
	std::vector<std::string> test_urls = {
		"https://www.facebook.com",
		"https://www.instagram.com",
		"https://twitter.com",
		"https://www.snapchat.com",
		"https://www.reddit.com",
		"https://www.patreon.com",
		"https://www.linkedin.com",
		"https://www.soundcloud.com",
		"https://www.tiktok.com",
		"https://signal.org",
		"https://www.viber.com",
		"https://www.whatsapp.com",
		"https://www.apple.com/facetime",
		"https://www.telegram.org",
		"https://www.roblox.com",
		"https://www.rutracker.org",
		"https://www.envato.com",
		"https://www.metacritic.com",
		"https://www.tidal.com",
		"https://www.pinterest.com",
		"https://www.bbc.com",
		"https://www.cnn.com",
		"https://www.nytimes.com",
		"https://www.washingtonpost.com",
		"https://www.theguardian.com",
		"https://www.euronews.com",
		"https://www.radiofreeeurope.org",
		"https://www.dw.com",
		"https://www.repubblica.it",
		"https://meduza.io",
		"https://themoscowtimes.com",
		"https://openrussia.org",
		"https://imrussia.org",
		"https://www.protonvpn.com",
		"https://www.nordvpn.com",
		"https://www.expressvpn.com",
		"https://www.vpnservice.com",
		"https://www.google.com",
		"https://www.youtube.com",
		"https://www.github.com",
		"https://www.stackoverflow.com",
		"https://vk.com",
		"https://zona.media",
		"https://baltnews.com",
		"https://discord.com",
		"https://www.chess.com",
		"https://www.quora.com",
		"https://www.archive.org",
		"https://www.fbi.gov",
		"https://www.cia.gov",
		"https://www.npr.org",
		"https://www.amnesty.org",
		"https://www.hrw.org",
		"https://www.theintercept.com",
		"https://www.euronews.com",
		"https://www.voanews.com",
		"https://www.clarin.com",
		"https://www.rai.it",
		"https://www.nytimes.com",
		"https://www.washingtonexaminer.com",
		"https://www.bellingcat.com",
		"https://www.tjournal.com",
		"https://www.kitabook.net",
		"https://www.freedomletters.org",
		"https://www.goodreads.com",
		"https://www.ruslania.com",
		"https://www.bookamaro.com",
		"https://www.nuntiare.org",
		"https://www.museumoflgbthistory.org",
		"https://www.truehistory.org",
		"https://www.spotify.com",
		"https://www.pornhub.com",
		"https://www.xhamster.com",
		"https://www.redtube.com",
		"https://www.youjizz.com",
		"https://www.xnxx.com",
		"https://www.ero‑xnxx.com",
		"https://www.booking.com",
		"https://www.airbnb.com",
		"https://www.tripadvisor.com",
		"https://www.booking.com",
		"https://www.expedia.com",
		"https://www.hotels.com",
		"https://www.skyscanner.net",
		"https://www.discord.gg"
	};


	const size_t NTHREADS = std::stoi(argv[1]);
	std::ifstream lists("lists");
	std::ofstream out_conf("config.json", std::ios::trunc);
	
	std::vector<std::pair<std::string, uint16_t>> configs[NTHREADS];
	
	json::object results[NTHREADS], final;
	std::string line;
	uint32_t start_port = 10809, port = start_port;
	size_t idx = 0;

	while(getline(lists, line) && port <= 0xFFFF)
		configs[idx++ % NTHREADS].push_back({line, static_cast<uint16_t>(port++)});
	
	/* initialize results and final with default values */
	final["inbounds"] = json::array{};
	final["outbounds"] = json::array{};
	final["routing"] = json::object{
		{"domainStrategy", "AsIs"},
		{"rules", json::array{}}
	};
	for(int i = 0; i < NTHREADS; ++i)
		results[i] = final;
	std::thread threads[NTHREADS];

	for(int i = 0; i < NTHREADS; ++i) {
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

	for(int i = 0; i < NTHREADS; ++i)
		threads[i].join();
	
	/* merge */
	for(int i = 0; i < NTHREADS; ++i) {
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
	
	process::child c("/usr/local/bin/xray", "run", "-c", "config.json",
					 process::std_out > process::null,
					 process::std_err > process::null);
	std::this_thread::sleep_for(std::chrono::seconds(5));
	
	curl_global_init(CURL_GLOBAL_DEFAULT);
	
	for(int i = 0; i < NTHREADS; ++i) {
		threads[i] = std::thread([&test_urls, &configs, i] {
			std::ostringstream stdout_buf, stderr_buf;
			for(const auto& [conf, port] : configs[i]) {
				std::random_device rd;
				std::mt19937 gen(rd());
				std::uniform_int_distribution<size_t> dist(0, test_urls.size() - 1);
				const auto& rand_url = test_urls[dist(gen)];
				const auto info = httpcheck(port, rand_url, 5, stderr_buf);
				
				if(info.http_code != 0) {
					const auto metric = geometric_with_proxy(port, 5, stderr_buf);
					stdout_buf << "\nConfiguration: " << conf << "\n";
					stdout_buf << "====================ipinfo.io/json====================\n";
					stdout_buf << "IP: " << metric.ip << "\n";
					stdout_buf << "Country: " << metric.country << "\n";
					stdout_buf << "Region: " << metric.region << "\n";
					stdout_buf << "City: " << metric.city << "\n";
					stdout_buf << "Time zone: " << metric.timezone << "\n";
					stdout_buf << "ORG: " << metric.org << "\n";
					stdout_buf << "LOC: " << metric.loc << "\n";
					stdout_buf << "Postal: " << metric.postal << "\n";
					stdout_buf << "======================================================\n";
					stdout_buf << "URL: " << rand_url << "\n";
					stdout_buf << "HTTP Code: " << info.http_code << "\n";
					stdout_buf << "DNS lookup: " << info.t_dns*1000 << " ms\n";
					stdout_buf << "Connect: " << info.t_connect*1000 << " ms\n";
					stdout_buf << "TLS handshake: " << info.t_tls*1000 << " ms\n";
					stdout_buf << "TTFB: " << info.t_ttfb*1000 << " ms\n";
					stdout_buf << "Total time: " << info.t_total*1000 << " ms\n";
					stdout_buf << "Downloaded: " << info.size/1024.0 << " KB\n";
					stdout_buf << "Speed: " << info.speed/1024.0 << " KB/s\n\n";
					
				}
			}
			std::lock_guard<std::mutex> lk(stdout_mtx);
			std::cout << stdout_buf.str();
			std::cerr << stderr_buf.str();
		});
	}
	for(int i = 0; i < NTHREADS; ++i)
		threads[i].join();
	curl_global_cleanup();
	return 0;
}
