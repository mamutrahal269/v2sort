#include <fstream>
#include <string>
#include <string_view>

std::string read_file(std::string_view path) {
	std::ifstream file;
	file.exceptions(std::ios::failbit | std::ios::badbit);
	file.open(path.data());
	return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
