#pragma once
#include <boost/json.hpp>
#include <exception>
#include <string>
#include <string_view>

class inval_proto : public std::invalid_argument {
  public:
	explicit inval_proto(std::string_view msg) : std::invalid_argument(std::string(msg)) {}

	explicit inval_proto(const std::string& msg) : std::invalid_argument(msg) {}

	explicit inval_proto(const char* msg) : std::invalid_argument(msg) {}
};
boost::json::object mkvless(const std::string_view vless, const std::string_view tag);
boost::json::object mkss(const std::string_view ss, const std::string_view tag);
boost::json::object mktrojan(const std::string_view trojan, const std::string_view tag);
boost::json::object mkvmess(const std::string_view vmess, const std::string_view tag);
boost::json::object mkhttp(const std::string_view http, const std::string_view tag);
