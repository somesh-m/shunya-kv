#pragma once
#include <seastar/core/smp.hh>
#include <string>
#include <string_view>
#include <utility>

namespace shunyakv::proto {
inline std::string_view ltrim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    return s;
}
inline std::string_view rtrim(std::string_view s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                          s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}
inline std::string_view trim(std::string_view s) { return rtrim(ltrim(s)); }

// Split on first ASCII space/tab. Returns {head, tail_without_leading_ws}
inline std::pair<std::string_view, std::string_view>
split_first(std::string_view s) {
    s = trim(s);
    auto p = s.find_first_of(" \t");
    if (p == std::string_view::npos)
        return {s, std::string_view{}};
    return {s.substr(0, p), ltrim(s.substr(p + 1))};
}

inline std::string to_upper(std::string_view s) {
    std::string u;
    u.reserve(s.size());
    for (unsigned char c : s)
        u.push_back(static_cast<char>(std::toupper(c)));
    return u;
}

// Stateless, so leave it inline
inline unsigned shard_for_key(std::string_view key) {
    return static_cast<unsigned>(std::hash<std::string_view>{}(key) %
                                 seastar::smp::count);
}

// For SET: parse "<key> <value...>" -> {key, value}; empty sv on failure
inline std::pair<std::string_view, std::string_view>
parse_set_kv(std::string_view rest) {
    rest = trim(rest);
    auto [k, tail] = split_first(rest);
    if (k.empty() || tail.empty())
        return {std::string_view{}, std::string_view{}};
    return {k, rtrim(tail)}; // keep spaces inside value; strip trailing CR/LF
}

inline bool ieq(const std::string_view &a, const char *b) {
    if (a.size() != std::strlen(b))
        return false;
    for (size_t i = 0; i < a.size(); i++) {
        char ca = a[i];
        char cb = b[i];
        if ('a' <= ca && ca <= 'z')
            ca = char(ca - 'a' + 'A');
        if ('a' <= cb && cb <= 'z')
            cb = char(cb - 'a' + 'A');
        if (ca != cb)
            return false;
    }
    return true;
}

} // namespace shunyakv::proto
