#include <string>
#include <string_view>

std::string							  read_file(std::string_view path);
template <typename t> std::pair<t, t> subrange(t min_val, t max_val, size_t n, size_t index) {
	if (n == 0 || index >= n) throw std::out_of_range("invalid n or index");

	if (max_val < min_val) throw std::invalid_argument("max_val must be >= min_val");

	t total = max_val - min_val + 1;	 // общая длина диапазона
	t base	= total / static_cast<t>(n); // минимальный размер поддиапазона
	t rem	= total % static_cast<t>(n); // остаток

	t start = min_val + static_cast<t>(index) * base + static_cast<t>(index < rem ? index : rem);

	t length = base + (index < rem ? 1 : 0);
	t end	 = start + length - 1;

	return {start, end};
}
