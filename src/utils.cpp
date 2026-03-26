#include "utils.hpp"
#include <algorithm>
#include <boost/beast/core/detail/base64.hpp>
#include <fstream>
#include <ranges>
#include <string>
#include <string_view>

using namespace boost;
std::optional<std::string> decode64(std::string encoded) {
	encoded.erase(std::remove_if(encoded.begin(), encoded.end(), [](unsigned char c) { return std::isspace(c); }), encoded.end());
	for (char c : encoded)
		if (!std::isalnum((unsigned char) c) && c != '+' && c != '/' && c != '=') return std::nullopt;
	while (encoded.size() % 4) encoded += '=';

	static const std::string b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string				 decoded;
	decoded.reserve(encoded.size() / 4 * 3);
	uint32_t val  = 0;
	int		 valb = -8;
	for (unsigned char c : encoded) {
		if (c == '=') break;
		auto pos = b64chars.find(c);
		if (pos == std::string::npos) return std::nullopt;
		val = (val << 6) | pos;
		valb += 6;
		if (valb >= 0) {
			decoded.push_back((val >> valb) & 0xFF);
			valb -= 8;
		}
	}
	return decoded;
}
std::string read_file(std::string_view path) {
	std::ifstream file;
	file.exceptions(std::ios::failbit | std::ios::badbit);
	file.open(path.data());
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
std::string encode64(std::string_view input) {
	using namespace boost::beast::detail;
	std::string encoded;
	encoded.resize(base64::encoded_size(input.size()));
	auto len = base64::encode(encoded.data(), input.data(), input.size());
	encoded.resize(len);
	return encoded;
}
std::string str_tolower(std::string_view s) {
	std::string lower;
	std::ranges::for_each(s, [&lower](char c) { lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
	return lower;
}
