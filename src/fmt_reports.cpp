#include "fmt_reports.hpp"
#include "utils.hpp"
#include <boost/json.hpp>
#include <boost/url.hpp>
#include <sstream>
#include <string>

using namespace boost;
std::string fmt_fragment(std::string_view url_str, const v2sort_params& params, const proxy_report& r) {
	urls::url url;
	if (system::result<urls::url> result = urls::parse_uri(url_str.data()); !result) [[unlikely]]
		throw std::runtime_error("invalid url");
	else
		url = result.value();
	std::string fragment = params.fragment_format.value();
	auto		replace	 = [&](std::string_view key, const std::string& value) {
		std::string token = "%" + std::string(key) + "%";
		size_t		pos	  = 0;
		while ((pos = fragment.find(token, pos)) != std::string::npos) {
			fragment.replace(pos, token.length(), value);
			pos += value.length();
		}
	};
	replace("url", r.url);
	replace("ip", r.geo.ip);
	replace("country", r.geo.country);
	replace("city", r.geo.city);
	replace("region", r.geo.region);
	replace("speed", std::to_string(r.speed.value_or(0)));
	replace("speed_kib", std::to_string(static_cast<double>(r.speed.value_or(0)) / 1024.0));
	if (url.scheme() == "vmess") {
		json::object json_vmess = json::parse(decode64(url.c_str() + 8)).as_object(); /* exception ?*/
		json_vmess["ps"]		= fragment;
		return "vmess://" + encode64(json::serialize(json_vmess));
	}
	url.set_fragment(fragment);
	return url.c_str();
}
std::string str_report(out_style style, const std::vector<proxy_report>& reports) {
	std::ostringstream out;
	switch (style) {
	case out_style::raw: {
		for (const auto& r : reports) {
			out << r.url << '\n';
		}
		break;
	}
	case out_style::json: {
		json::array reports_json;
		for (const auto& r : reports) {
			reports_json.push_back(json::object{{"url", r.url},
												{"ip", r.geo.ip},
												{"country", r.geo.country},
												{"city", r.geo.city},
												{"region", r.geo.region},
												{"speed", r.speed.value_or(0)}});
		}
		out << reports_json;
		break;
	}
	case out_style::human: {
		for (const auto& r : reports) {
			out << "==================================\n"
				<< "URL: " << r.url << '\n'
				<< "IP: " << r.geo.ip << '\n'
				<< "Country: " << r.geo.country << '\n'
				<< "Speed: " << (float) r.speed.value_or(0) / 1024 << "KiB/s\n"
				<< "==================================\n";
		}
		break;
	}
	}
	return out.str();
}
