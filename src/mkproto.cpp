#include "mkproto.hpp"
#include "utils.hpp"
#include <algorithm>
#include <boost/beast/core/detail/base64.hpp>
#include <boost/json.hpp>
#include <boost/locale.hpp>
#include <boost/log/trivial.hpp>
#include <boost/url.hpp>
#include <boost/url/pct_string_view.hpp>
#include <format>
#include <iostream>
#include <optional>
using namespace boost;
namespace {
[[gnu::always_inline]]
inline std::optional<std::string> query_val(const urls::params_view query, const std::string_view key, bool quiet) {
	if (const auto it = query.find(key); it != query.end()) {
		if ((*it).value.empty() && !quiet)
			throw inval_proto(std::format("parameter '{}' not found", key));
		else if ((*it).value.empty() && quiet)
			return std::nullopt;
		else
			return std::make_optional((*it).value);
	} else if (quiet)
		return std::nullopt;
	else
		throw inval_proto(std::format("parameter '{}' not found", key));
}
[[gnu::always_inline]]
inline json::array str2arr(const std::string_view str) {
	json::array		  result;
	std::stringstream ss{std::string(str)};
	std::string		  token;
	while (std::getline(ss, token, ',')) result.emplace_back(token);
	return result;
}
json::object streamSettings_gen(const urls::url_view url) {
	const urls::params_view query = strstr(url.buffer().data(), "?#") ? urls::params_view{} : url.params();
	json::object settings		  = {{"network", query_val(query, "type", 1).value_or(url.scheme().starts_with("hy") ? "hysteria" : "raw")},
									 {"security", query_val(query, "security", 1).value_or("none")}};
	if (const std::string& security = query_val(query, "security", 1).value_or(""); security == "tls") {
		settings["tlsSettings"] = json::object{{"serverName", query_val(query, "sni", 1).value_or("")},
											   {"alpn", str2arr(query_val(query, "alpn", 1).value_or("h2,http/1.1"))},
											   {"fingerprint", query_val(query, "fp", 1).value_or("chrome")},
											   {"echConfigList", query_val(query, "ech", 1).value_or("")},
											   {"pinnedPeerCertSha256", query_val(query, "pcs", 1).value_or("")},
											   {"allowInsecure", query_val(query, "insecure", 1).value_or("") == "1"}};
	} else if (security == "reality") {
		settings["realitySettings"] = json::object{
			//{"target", query_val(query, "sni", 0) + ':' + "443"},
			{"serverName", query_val(query, "sni", 1).value_or("")},	   {"publicKey", query_val(query, "pbk", 0).value()},
			{"shortId", query_val(query, "sid", 1).value_or("")},		   {"mldsa65Verify", query_val(query, "pqv", 1).value_or("")},
			{"fingerprint", query_val(query, "fp", 1).value_or("chrome")}, {"spiderX", query_val(query, "spx", 1).value_or("/")}};
	}
	if (const std::string& type = query_val(query, "type", 1).value_or(url.scheme().starts_with("hy") ? "hysteria" : "raw");
		type == "xhttp") {
		settings["xhttpSettings"] = json::object{{"host", query_val(query, "host", 1).value_or("")},
												 {"path", query_val(query, "path", 1).value_or("")},
												 {"mode", query_val(query, "mode", 1).value_or("auto")}};
		if (query.contains("extra")) {
			auto extra = json::parse(urls::pct_string_view(query_val(query, "extra", 0).value()).decode());
			if (extra.is_object()) settings["xhttpSettings"].as_object()["extra"] = extra.as_object();
		}
	} else if (type == "kcp") {
		settings["kcpSettings"] = json::object{{"mtu", query_val(query, "mtu", 0).value()},
											   {"tti", query_val(query, "tti", 0).value()},
											   {"uplinkCapacity", query_val(query, "uplinkCapacity", 0).value()},
											   {"downlinkCapacity", query_val(query, "downlinkCapacity", 0).value()},
											   {"congestion", query_val(query, "congestion", 0).value()},
											   {"readBufferSize", query_val(query, "readBufferSize", 0).value()},
											   {"writeBufferSize", query_val(query, "writeBufferSize", 0).value()}};
	} else if (type == "grpc") {
		settings["grpcSettings"] = json::object{{"authority", query_val(query, "authority", 1).value_or("")},
												{"serviceName", query_val(query, "serviceName", 1).value_or("")},
												{"user_agent", query_val(query, "user_agent", 1).value_or("")},
												{"multiMode", query_val(query, "multiMode", 1).value_or("") == "true"}};
	} else if (type == "httpupgrade") {
		settings["httpupgradeSettings"] =
			json::object{{"path", query_val(query, "path", 1).value_or("")}, {"host", query_val(query, "host", 1).value_or("")}};
	} else if (type == "ws") {
		settings["wsSettings"] =
			json::object{{"path", query_val(query, "path", 1).value_or("/")}, {"host", query_val(query, "host", 1).value_or("")}};
	} else {
		settings["rawSettings"] = json::object{{"header", json::object{{"type", query_val(query, "headerType", 1).value_or("none")}}}};
	}
	if (query_val(query, "obfs", 1).has_value()) {
		std::string obfs	  = query_val(query, "obfs", 0).value();
		settings["finalmask"] = json::object{{"udp", json::array{json::object{{"type", obfs}}}}};
		auto& udp0			  = settings["finalmask"].as_object()["udp"].as_array()[0].as_object();
		if (obfs == "header-dns" || obfs == "xdns") udp0["settings"] = json::object{{"domain", query_val(query, "obfs-domain", 0).value()}};
		if (obfs == "mkcp-aes128gcm" || obfs == "salamander")
			udp0["settings"] = json::object{{"password", query_val(query, "obfs-password", 0).value()}};
	}

	if (url.scheme().starts_with("hy")) {
		settings["hysteriaSettings"] = json::object{{"version", (url.scheme().back() == '2') + 1}, {"auth", url.password()}};
	}
	return settings;
}
} // namespace
json::object mkvless(const std::string_view vless, const std::string_view tag) {
	if (!vless.starts_with("vless://")) [[unlikely]]
		throw inval_proto("invalid vless url");
	urls::url url;
	if (system::result<urls::url> result = urls::parse_uri(vless.data()); !result)
		throw inval_proto(result.error().message());
	else
		url = result.value();
	url.normalize();
	const auto query = strstr(url.c_str(), "?#") ? urls::params_view{} : url.params();
	return {
		{"protocol", "vless"},
		{"settings",
		 json::object{{"vnext", json::array{json::object{
									{"address", url.host()},
									{"port", std::stoi(url.port())},
									{"users", json::array{json::object{{"id", url.user()},
																	   {"encryption", "none"},
																	   {"flow", query_val(query, "flow", 1).value_or("xtls-rprx-vision")},
																	   {"level", 0}}}}}}}}},
		{"streamSettings", streamSettings_gen(url)},
		{"tag", tag}};
}
json::object mkss(const std::string_view ss, const std::string_view tag) {
	if (!ss.starts_with("ss://")) [[unlikely]]
		throw inval_proto("invalid ss url");
	urls::url url;
	if (system::result<urls::url> result = urls::parse_uri(ss.data()); !result)
		throw inval_proto(result.error().message());
	else
		url = result.value();
	url.normalize();
	std::string decoded;
	{
		size_t		start = url.scheme().size() + 3;
		std::string mp;
		size_t		pos = ss.find('@', start);
		if (pos != std::string::npos)
			mp = ss.substr(start, pos - start);
		else
			throw inval_proto("invalid ss url");

		if (mp.find(':') == std::string::npos) {
			std::replace(mp.begin(), mp.end(), '-', '+');
			std::replace(mp.begin(), mp.end(), '_', '/');
			if (auto ret = decode64(mp); ret.has_value())
				decoded = ret.value();
			else
				throw inval_proto("invalid ss base64");
		} else
			decoded = url.userinfo();
	}
	auto		delim_pos = decoded.find(':');
	std::string method	  = delim_pos == std::string::npos ? decoded : decoded.substr(0, delim_pos);
	std::string passwd	  = delim_pos == std::string::npos ? decoded : decoded.substr(delim_pos + 1);
	const auto	query	  = strstr(url.c_str(), "?#") ? urls::params_view{} : url.params();
	return {{"protocol", "shadowsocks"},
			{"settings",
			 json::object{
				 {"servers", json::array{json::object{{"address", url.host()},
													  {"port", std::stoi(url.port())},
													  {"email", query_val(query, "email", 1).value_or("")},
													  {"method", method},
													  {"password", passwd},
													  {"uot", query_val(query, "uot", 1).value_or("") == "true"},
													  {"UoTVersion", std::stoi(query_val(query, "UoTVersion", 1).value_or("1"))},
													  {"level", 0}}}},
			 }},
			{"streamSettings", streamSettings_gen(url)},
			{"tag", tag}};
}
json::object mktrojan(const std::string_view trojan, const std::string_view tag) {
	if (!trojan.starts_with("trojan://")) [[unlikely]]
		throw inval_proto("invalid trojan url");
	urls::url url;
	if (system::result<urls::url> result = urls::parse_uri(trojan.data()); !result)
		throw inval_proto(result.error().message());
	else
		url = result.value();
	url.normalize();
	const auto query = strstr(url.c_str(), "?#") ? urls::params_view{} : url.params();
	return {{"protocol", "trojan"},
			{"settings",
			 json::object{
				 {"servers", json::array{json::object{{"address", url.host()},
													  {"port", std::stoi(url.port())},
													  {"email", query_val(query, "email", 1).value_or("")},
													  {"password", url.userinfo()},
													  {"level", 0}}}},
			 }},
			{"streamSettings", streamSettings_gen(url)},
			{"tag", tag}};
}
json::object mkvmess(const std::string_view vmess, const std::string_view tag) {
	if (!vmess.starts_with("vmess://")) [[unlikely]]
		throw inval_proto("invalid vmess url");
	urls::url url;
	if (system::result<urls::url> result = urls::parse_uri(vmess.data()); !result)
		throw inval_proto(result.error().message());
	else
		url = result.value();
	url.normalize();
	urls::params_ref query = url.params();
	json::object	 params;
	if (auto ret = decode64(vmess.data() + 8); ret.has_value()) {
		url.clear();
		try {
			params = json::parse(ret.value()).as_object();
		} catch (const json::system_error&) {
			throw inval_proto("invalid vmess json");
		}
		url.set_scheme("vmess");
		url.set_host(params["add"].as_string());
		url.set_port(params["port"].is_string()
						 ? std::string(params["port"].as_string())
						 : std::to_string(params["port"].is_int64() ? params["port"].as_int64() : (int64_t) params["port"].as_uint64()));
		url.set_userinfo(params["id"].as_string());

		query = url.params();

		for (const auto& [k, v] : params) {
			std::string value;

			if (v.is_string())
				value = std::string(v.as_string());
			else if (v.is_uint64())
				value = std::to_string(v.as_uint64());
			else if (v.is_int64())
				value = std::to_string(v.as_int64());
			else if (v.is_double())
				value = std::to_string(v.as_double());
			else if (v.is_bool())
				value = v.as_bool() ? "true" : "false";
			else if (v.is_null())
				value = "null";
			else
				value = json::serialize(v);

			query.set(k, value);
		}

		query.set("security", query_val(query, "tls", 1).value_or(""));
		query.set("headerType", query_val(query, "type", 1).value_or("none"));
		query.set("type", query_val(query, "net", 1).value_or("tcp"));
		query.set("encryption", query_val(query, "scy", 1).value_or("none"));
	}
	if (!url.has_port()) throw inval_proto("invalid vmess url");
	return {
		{"protocol", "vmess"},
		{"settings",
		 json::object{
			 {"vnext",
			  json::array{json::object{
				  {"address", url.host()},
				  {"port", std::stoi(url.port())},
				  {"users", json::array{json::object{
								{"id", url.user()},
								{"security", query_val(query, "encryption", 1).value_or("none")},
								{"alterId", std::stoi(query_val(query, "aid", 1).value_or(query_val(query, "alterId", 1).value_or("0")))},
								{"level", 0}}}}}}}}},
		{"streamSettings", streamSettings_gen(url)},
		{"tag", tag}};
}
json::object mkhttp(const std::string_view http, const std::string_view tag) {
	if (!http.starts_with("http://")) [[unlikely]]
		throw inval_proto("invalid http/https url");
	urls::url url;
	if (system::result<urls::url> result = urls::parse_uri(http.data()); !result)
		throw inval_proto(result.error().message());
	else
		url = result.value();
	url.normalize();
	const auto query = strstr(url.c_str(), "?#") ? urls::params_view{} : url.params();
	return {{"protocol", "http"},
			{"settings",
			 json::object{{"servers", json::array{json::object{
										  {"address", url.host()},
										  {"port", std::stoi(url.port().empty() ? (url.scheme() == "https" ? "443" : "80") : url.port())},
										  {"users", json::array{json::object{{"user", url.user()}, {"pass", url.password()}}}}}}}}},
			{"streamSettings", streamSettings_gen(url)},
			{"tag", tag}};
}
json::object mksocks(const std::string_view socks, const std::string_view tag) {
	if (!socks.starts_with("socks")) [[unlikely]]
		throw inval_proto("invalid socks url");
	urls::url url;
	if (system::result<urls::url> result = urls::parse_uri(socks.data()); !result)
		throw inval_proto(result.error().message());
	else
		url = result.value();
	url.normalize();
	const auto query = strstr(url.c_str(), "?#") ? urls::params_view{} : url.params();
	return {{"protocol", "socks"},
			{"settings", json::object{{"servers", json::array{json::object{{"address", url.host()},
																		   {"port", std::stoi(url.port())},
																		   {"user", url.user()},
																		   {"pass", url.password()},
																		   {"email", query_val(query, "email", 1).value_or("")}}}}}},
			{"tag", tag}};
}
json::object mkhysteria(const std::string_view hy, const std::string_view tag) {
	if (!hy.starts_with("hy")) [[unlikely]]
		throw inval_proto("invalid hysteria url");
	urls::url url;
	if (system::result<urls::url> result = urls::parse_uri(hy.data()); !result)
		throw inval_proto(result.error().message());
	else
		url = result.value();
	url.normalize();
	const auto query = strstr(url.c_str(), "?#") ? urls::params_view{} : url.params();
	return {
		{"protocol", "hysteria"},
		{"settings", json::object{{"address", url.host()}, {"port", std::stoi(url.port())}, {"version", (url.scheme().back() == '2') + 1}}},
		{"streamSettings", streamSettings_gen(url)}};
}
