#pragma once

#include "geodata.hpp"
#include "net.hpp"
#include "v2sort.hpp"
#include <string>
#include <string_view>

std::string fmt_fragment(std::string_view url_str, const v2sort_params& params, const proxy_report& r);
std::string str_report(out_style style, const std::vector<proxy_report>& reports);
