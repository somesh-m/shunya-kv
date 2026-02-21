#pragma once
#include <seastar/core/sstring.hh>
#include <string_view>
#include <vector>

namespace resp {
using Array = std::vector<seastar::sstring>;    // keep existing
using ArgvView = std::vector<std::string_view>; // NEW
} // namespace resp
