#pragma once
#include <seastar/core/future.hh>
#include <seastar/net/api.hh>
#include <string_view>
#include <utility>

namespace shunyakv {
inline std::pair<std::string_view, std::string_view>
split_cmd(std::string_view line) {
    auto s = line;
    auto eat_ws = [&] {
        while (!s.empty() && s.front() == ' ') {
            s.remove_prefix(1);
        }
    };
    eat_ws();
    size_t i = 0;
    while (i < s.size() && s[i] != ' ')++ i;
    std::string_view cmd = s.substr(0, i);
    if (i < s.size()) {
        s.remove_prefix(i + 1);
    } else {
        s.remove_prefix(i);
    }
    eat_ws();
    return {cmd, s};
}
} // namespace shunyakv
