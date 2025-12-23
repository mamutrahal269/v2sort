#include "mkproto.hpp"
#include <boost/beast/core/detail/base64.hpp>
#include <boost/url/pct_string_view.hpp>
#include <boost/locale.hpp>
#include <boost/json.hpp>
#include <boost/url.hpp>
#include <algorithm>
#include <optional>
#include <format>
using namespace boost;
namespace {
	std::string decode64(std::string encoded) {
		encoded.erase(
			std::remove_if(encoded.begin(), encoded.end(),
				[](unsigned char c) { return c == ' ' || c == '\n'; }),
			encoded.end()
		);

		char decoded[beast::detail::base64::decoded_size(encoded.size())];
		auto len = beast::detail::base64::decode(decoded, encoded.data(), encoded.size());
		return std::string(decoded, len.first);
	}
	std::optional<std::string> query_val(const urls::params_view query, const std::string_view key, bool quiet) {
		if(const auto it = query.find(key); it != query.end()) return (*it).value.empty() ? std::nullopt : std::make_optional((*it).value);
		else if(quiet) return std::nullopt;
		else throw inval_proto(std::format("parameter '{}' not found", key));
	}
	json::array str2arr(const std::string_view str) {
		json::array result;
		std::stringstream ss{std::string(str)};
		std::string token;
		while (std::getline(ss, token, ','))
			result.emplace_back(token);
		return result;
	}
	json::object streamSettings_gen(const urls::url_view url) {
		const urls::params_view query = url.params();
		json::object settings = {
			{"network", query_val(query, "type", 1).value_or("tcp")},
			{"security", query_val(query, "security", 1).value_or("none")}
		};
		if(const std::string& security = query_val(query, "security", 1).value_or(""); security == "tls") {
			settings["tlsSettings"] = json::object {
				{"serverName", query_val(query, "sni", 0).value()},
				{"alpn", str2arr(query_val(query, "alpn", 1).value_or("h2,http/1.1"))},
				{"rejectUnknownSni", false},
				{"fingerprint", query_val(query, "fp", 1).value_or("chrome")}
			};
		} else if(security == "reality") {
			settings["realitySettings"] = json::object {
				//{"target", query_val(query, "sni", 0) + ':' + "443"},
				{"serverName", query_val(query, "sni", 0).value()},
				{"publicKey",  query_val(query, "pbk", 0).value()},
				{"shortId", query_val(query, "sid", 0).value()},
				{"fingerprint", query_val(query, "fp", 1).value_or("chrome")},
				{"spiderX", query_val(query, "spx", 1).value_or("/")}
			};
		}
		if(const std::string& type = query_val(query, "type", 1).value_or(""); type == "xhttp") {
			settings["xhttpSettings"] = json::object {
				{"host", query_val(query, "host", 0).value()},
				{"path", query_val(query, "path", 0).value()},
				{"mode", query_val(query, "mode", 1).value_or("auto")}
			};
			if(query.contains("extra"))
				settings["xhttpSettings"].as_object()["extra"] = json::parse(urls::pct_string_view(query_val(query, "extra", 0).value()).decode()).as_object();
		} else if(type == "kcp") {
			settings["kcpSettings"] = json::object {
				{"mtu", query_val(query, "mtu", 0).value()},
				{"tti", query_val(query, "tti", 0).value()},
				{"uplinkCapacity", query_val(query, "uplinkCapacity", 0).value()},
				{"downlinkCapacity", query_val(query, "downlinkCapacity", 0).value()},
				{"congestion", query_val(query, "congestion", 0).value()},
				{"readBufferSize", query_val(query, "readBufferSize", 0).value()},
				{"writeBufferSize", query_val(query, "writeBufferSize", 0).value()},
				{"seed", query_val(query, "seed", 0).value()},
				{"header", json::object {
						{"type", query_val(query, "headerType", 1).value_or("none")}
					}
				}
			};
			if(query_val(query, "headerType", 1).value_or("") == "dns") settings["kcpSettings"].as_object()["header"].as_object()["domain"] = query_val(query, "domain", 0).value();
		} else if(type == "grpc") {
			settings["grpcSettings"] = json::object{
				{"authority", query_val(query, "authority", 1).value_or("")},
				{"serviceName", query_val(query, "serviceName", 0).value()},
				{"multiMode", query_val(query, "multiMode", 1).value_or("") == "true"}
			};
		} else if(type == "httpupgrade") {
			settings["httpupgradeSettings"] = json::object {
				{"path", query_val(query, "path", 0).value()},
				{"host", query_val(query, "host", 0).value()}
			};
		} else if(type == "ws") {
			settings["wsSettings"] = json::object {
				{"path", query_val(query, "path", 0).value()},
				{"host", query_val(query, "host", 0).value()}
			};
		} else {
				settings["tcpSettings"] = json::object {
					{"header", json::object {
							{"type", query_val(query, "headerType", 1).value_or("none")}
						}
					}
			};
		}
		return settings;
	}
}
json::object mkvless(const std::string_view vless, const std::string_view tag) {
	if(vless.compare(0, 8, std::string("vless://"), 0, 8)) throw inval_proto("invalid vless url");
	urls::url url;
	if(system::result<urls::url> result = urls::parse_uri(vless.data()); !result) throw inval_proto(result.error().message());
	else url = result.value();
	url.normalize();
	const auto query = url.params();
	return {
		{"protocol", "vless"},
		{"settings", json::object {
				{"vnext", json::array {
						json::object {
							{"address", url.host()},
							{"port", std::stoi(url.port())},
							{"users", json::array {
									json::object {
										{"id", url.user()},
										{"encryption", "none"},
										{"flow", query_val(query, "flow", 1).value_or("xtls-rprx-vision")},
										{"level", 0}
									}
								}
							}
						}
					}
				}
			}
		},
		{"streamSettings", streamSettings_gen(url)},
		{"tag", tag}
	};
}
json::object mkss(const std::string_view ss, const std::string_view tag) {
	if(ss.compare(0, 4, std::string("ss://"), 0, 4)) throw inval_proto("invalid ss url");
	urls::url url;
	if(system::result<urls::url> result = urls::parse_uri(ss.data()); !result) throw inval_proto(result.error().message());
	else url = result.value();
	url.normalize();
	std::string decoded = decode64(url.userinfo());
	auto delim_pos = decoded.find(':');
	std::string method = delim_pos == std::string::npos ?
		decoded :
		decoded.substr(0, delim_pos);
	std::string passwd = delim_pos == std::string::npos ?
		decoded :
		decoded.substr(delim_pos + 1);
	const auto query = url.params();
	return {
		{"protocol", "shadowsocks"},
		{"settings", json::object {
				{"servers", json::array {
						json::object {
							{"address", url.host()},
							{"port", std::stoi(url.port())},
							{"email", query_val(query, "email", 1).value_or("")},
							{"method", method},
							{"password", passwd},
							{"uot", query_val(query, "uot", 1).value_or("") == "true"},
							{"UoTVersion", std::stoi(query_val(query, "UoTVersion", 1).value_or("1"))},
							{"level", 0}
						}
					}
				},
			}
		},
		{"streamSettings", streamSettings_gen(url)},
		{"tag", tag}
	};
}
json::object mktrojan(const std::string_view trojan, const std::string_view tag) {
	if(trojan.compare(0, 9, std::string("trojan://"), 0, 9)) throw inval_proto("invalid trojan url");
	urls::url url;
	if(system::result<urls::url> result = urls::parse_uri(trojan.data()); !result) throw inval_proto(result.error().message());
	else url = result.value();
	url.normalize();
	const auto query = url.params();
	return {
		{"protocol", "trojan"},
		{"settings", json::object {
				{"servers", json::array {
						json::object {
							{"address", url.host()},
							{"port", std::stoi(url.port())},
							{"email", query_val(query, "email", 1).value_or("")},
							{"password", url.userinfo()},
							{"level", 0}
						}
					}
				},
			}
		},
		{"streamSettings", streamSettings_gen(url)},
		{"tag", tag}
	};
}
json::object mkvmess(const std::string_view vmess, const std::string_view tag) {
	if(vmess.compare(0, 8, std::string("vmess://"), 0, 8)) throw inval_proto("invalid vmess url");
	urls::url url;
	if(system::result<urls::url> result = urls::parse_uri(vmess.data()); !result) throw inval_proto(result.error().message());
	else url = result.value();
	json::object params = json::parse(decode64(url.c_str() + 8)).as_object();
	url.set_host(params["add"].as_string());
	url.set_port(params["port"].as_string());
	url.set_user(params["id"].as_string());
	urls::params_ref query = url.params();
	for(const auto& [k, v] : params)
		query.set(k, v.as_string());
	query.set("security", params["tls"].as_string());
	query.set("type", params["net"].as_string());
	query.set("headerType", params["type"].as_string());
	return {
		{"protocol", "vmess"},
		{"settings", json::object {
				{"vnext", json::array {
						json::object {
							{"address", params["add"].as_string()},
							{"port", std::stoi(params["port"].as_string().c_str())},
							{"users", json::array {
									json::object {
										{"id", params["id"].as_string()},
										{"security", params["scy"].as_string()},
										{"level", 0}
									}
								}
							}
						}
					}
				}
			}
		},
		{"streamSettings", streamSettings_gen(url)},
		{"tag", tag}
	};
}
json::object mkhttp(const std::string_view http, const std::string_view tag) {
	if (http.compare(0, 7, std::string("http://"), 0, 7) &&
		http.compare(0, 8, std::string("https://"), 0, 8))  throw inval_proto("invalid http/https url");
	urls::url url;
	if(system::result<urls::url> result = urls::parse_uri(http.data()); !result) throw inval_proto(result.error().message());
	else url = result.value();
	url.normalize();
	const auto query = url.params();
	return {
		{"protocol", "http"},
		{"settings", json::object {
				{"servers", json::array {
						json::object {
							{"address", url.host()},
							{"port", std::stoi(url.port().empty() ? (url.scheme() == "https" ? "443" : "80") : url.port())},
							{"users", json::array {
									json::object {
										{"user", url.user()},
										{"pass", url.password()}
									}
								}
							}
						}
					}
				}
			}
		},
		{"streamSettings", streamSettings_gen(url)},
		{"tag", tag}
	};
}
