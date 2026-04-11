#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

std::string							  fix_url(const std::string url);
std::optional<std::string>			  decode64(std::string encoded);
std::string							  encode64(std::string_view input);
std::string							  read_file(std::string_view path);
std::string							  str_tolower(std::string_view s);
template <typename t> std::pair<t, t> subrange(t min_val, t max_val, size_t n, size_t index) {
	if (n == 0 || index >= n) throw std::out_of_range("invalid n or index");

	if (max_val < min_val) throw std::invalid_argument("max_val must be >= min_val");

	t total = max_val - min_val + 1;
	t base	= total / static_cast<t>(n);
	t rem	= total % static_cast<t>(n);

	t start = min_val + static_cast<t>(index) * base + static_cast<t>(index < rem ? index : rem);

	t length = base + (index < rem ? 1 : 0);
	t end	 = start + length - 1;

	return {start, end};
}
