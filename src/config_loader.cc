#include "config_loader.hh"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

static inline std::string_view trim(std::string_view s) {
    const size_t begin = s.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos) {
        return {};
    }
    const size_t end = s.find_last_not_of(" \t\r");
    return s.substr(begin, end - begin + 1);
}

static inline bool parse_bool(std::string_view v, bool &out) {
    if (v == "1" || v == "true" || v == "TRUE" || v == "on" || v == "ON" ||
        v == "yes" || v == "YES") {
        out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "FALSE" || v == "off" ||
        v == "OFF" || v == "no" || v == "NO") {
        out = false;
        return true;
    }
    return false;
}

static inline std::string_view maybe_unquote(std::string_view v) {
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
        return v.substr(1, v.size() - 2);
    }
    return v;
}

static inline bool parse_u64(std::string_view v, uint64_t &out) {
    uint64_t parsed = 0;
    const auto [ptr, ec] =
        std::from_chars(v.data(), v.data() + v.size(), parsed);
    if (ec == std::errc{} && ptr == v.data() + v.size()) {
        out = parsed;
        return true;
    }
    return false;
}

static inline bool parse_double(std::string_view v, double &out) {
    try {
        std::string s(v);
        size_t idx = 0;
        const double parsed = std::stod(s, &idx);
        if (idx == s.size()) {
            out = parsed;
            return true;
        }
    } catch (...) {
    }
    return false;
}

static inline bool parse_policy(std::string_view v,
                                eviction::EvictionPolicy &out) {
    if (v == "sieve" || v == "SIEVE" || v == "Sieve") {
        out = eviction::EvictionPolicy::Sieve;
        return true;
    }
    return false;
}

void load_config_file(db_config &cfg, const char *path) {
    namespace fs = std::filesystem;

    fs::path selected;
    std::ifstream in(path);
    if (in) {
        selected = fs::path(path);
    }

    if (!in) {
        const fs::path requested(path);
        if (!requested.is_absolute() && requested == "config.conf") {
            for (const fs::path &candidate :
                 {fs::path("../config.conf"), fs::path("../../config.conf")}) {
                in = std::ifstream(candidate);
                if (in) {
                    selected = candidate;
                    break;
                }
            }
        }
    }

    if (!in) {
        std::cerr << "config: unable to open '" << path
                  << "', using built-in defaults\n";
        return;
    }
    std::cerr << "config: loaded '" << selected.string() << "'\n";

    std::string line;
    while (std::getline(in, line)) {
        std::string_view sv = trim(line);
        if (sv.empty() || sv.front() == '#') {
            continue;
        }

        const size_t eq = sv.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }

        const std::string_view key = trim(sv.substr(0, eq));
        std::string_view value = trim(sv.substr(eq + 1));
        const size_t comment = value.find('#');
        if (comment != std::string_view::npos) {
            value = trim(value.substr(0, comment));
        }
        if (value.empty()) {
            continue;
        }
        value = maybe_unquote(value);

        if (key == "db_port" || key == "base_port" || key == "port") {
            unsigned parsed = 0;
            const auto [ptr, ec] = std::from_chars(
                value.data(), value.data() + value.size(), parsed);
            if (ec == std::errc{} && ptr == value.data() + value.size() &&
                parsed <= 65535u) {
                cfg.db_port = static_cast<uint16_t>(parsed);
            }
        } else if (key == "hash") {
            cfg.hash = seastar::sstring(value.data(), value.size());
        } else if (key == "policy") {
            eviction::EvictionPolicy parsed{};
            if (parse_policy(value, parsed)) {
                cfg.ev_config.policy = parsed;
            }
        } else if (key == "soft_trigger") {
            double parsed = 0.0;
            if (parse_double(value, parsed)) {
                cfg.ev_config.soft_.trigger = parsed;
            }
        } else if (key == "soft_stop") {
            double parsed = 0.0;
            if (parse_double(value, parsed)) {
                cfg.ev_config.soft_.stop = parsed;
            }
        } else if (key == "soft_budget") {
            uint64_t parsed = 0;
            if (parse_u64(value, parsed)) {
                cfg.ev_config.soft_.budget = parsed;
            }
        } else if (key == "soft_throttle") {
            bool parsed = false;
            if (parse_bool(value, parsed)) {
                cfg.ev_config.soft_.throttle = parsed;
            }
        } else if (key == "hard_trigger") {
            double parsed = 0.0;
            if (parse_double(value, parsed)) {
                cfg.ev_config.hard_.trigger = parsed;
            }
        } else if (key == "hard_stop") {
            double parsed = 0.0;
            if (parse_double(value, parsed)) {
                cfg.ev_config.hard_.stop = parsed;
            }
        } else if (key == "hard_budget") {
            uint64_t parsed = 0;
            if (parse_u64(value, parsed)) {
                cfg.ev_config.hard_.budget = parsed;
            }
        } else if (key == "hard_throttle") {
            bool parsed = false;
            if (parse_bool(value, parsed)) {
                cfg.ev_config.hard_.throttle = parsed;
            }
        } else if (key == "send_shard_details_on_connect") {
            bool parsed = false;
            if (parse_bool(value, parsed)) {
                cfg.send_shard_details_on_connect = parsed;
            }
        } else if (key == "page_size_goal") {
            uint64_t parsed = 0;
            if (parse_u64(value, parsed)) {
                cfg.page_size_goal = static_cast<std::size_t>(parsed);
            }
        } else if (key == "key_reserve") {
            uint64_t parsed = 0;
            if (parse_u64(value, parsed)) {
                cfg.key_reserve = static_cast<std::size_t>(parsed);
            }
        }
    }
}
