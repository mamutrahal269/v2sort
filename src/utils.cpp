#include "utils.hpp"
#include <algorithm>
#include <boost/beast/core/detail/base64.hpp>
#include <fstream>
#include <ranges>
#include <string>
#include <string_view>

using namespace boost;
std::string decode64(std::string encoded) {
	encoded.erase(std::remove_if(encoded.begin(), encoded.end(), [](unsigned char c) { return c == ' ' || c == '\n'; }), encoded.end());

	char decoded[beast::detail::base64::decoded_size(encoded.size())];
	auto len = beast::detail::base64::decode(decoded, encoded.data(), encoded.size());
	return std::string(decoded, len.first);
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
