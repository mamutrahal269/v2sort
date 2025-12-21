#include <boost/json.hpp>
#include <string>
#include <vector>
#include <utility>
#include <cstdint>

boost::json::object configgen(const std::vector<std::pair<std::string, uint16_t>>& list, const uint16_t start_port);
