#include "cmd_node_info.hh"

#include <charconv>
#include <limits>
#include <stdexcept>
#include <string>
using seastar::format;
namespace shunyakv {

seastar::sstring compute_hash_ranges() {
    std::vector<hash_range> v;

    const unsigned first_sid = g_node_cfg.port_offset ? 1u : 0u;
    const unsigned last_sid = (g_node_cfg.smp == 0 ? 0u : g_node_cfg.smp);
    if (g_node_cfg.smp == 0 || last_sid < first_sid) {
        return seastar::sstring("{}");
    }

    const unsigned D = last_sid - first_sid;
    v.reserve(D);

    const __uint128_t SPACE = (__uint128_t)1 << 64;

    for (unsigned i = 0; i < D; ++i) {
        const unsigned sid = first_sid + i;

        const __uint128_t lo128 = (SPACE * i) / D;
        const __uint128_t hi128 = (SPACE * (i + 1)) / D - 1;

        const uint32_t port32 =
            static_cast<uint32_t>(g_node_cfg.base_port) + sid;
        // TODO: validate port32 <= 65535 if you want strictness
        v.push_back(hash_range{static_cast<uint64_t>(lo128),
                               static_cast<uint64_t>(hi128), sid,
                               static_cast<uint16_t>(port32)});
    }
    seastar::sstring json;

    json += "{";
    json += format("\"epoch\":{},\"smp\":{},\"base_port\":{},\"port_"
                   "offset\":{},\"hash\":\"{}\",\"ranges\":[",
                   g_node_cfg.epoch, g_node_cfg.smp, g_node_cfg.base_port,
                   g_node_cfg.port_offset, g_node_cfg.hash);

    for (size_t i = 0; i < v.size(); ++i) {
        const auto &r = v[i];
        json += format("[\"{}\",\"{}\",{},{}]", r.lo, r.hi, r.shard, r.port);
        if (i + 1 < v.size())
            json += ",";
    }
    json += "]}";
    return json;
}

seastar::sstring compute_hash(uint16_t sid) {
    std::vector<hash_range> v;

    const unsigned first_sid = g_node_cfg.port_offset ? 1u : 0u;
    const unsigned last_sid = (g_node_cfg.smp == 0 ? 0u : g_node_cfg.smp);

    if (g_node_cfg.smp == 0 || last_sid < first_sid) {
        return seastar::sstring("{}");
    }

    const unsigned d = last_sid - first_sid;
    if (d == 0) {
        return seastar::sstring("{}");
    }

    if (sid < first_sid || sid >= last_sid) {
        return seastar::sstring("{}");
    }

    const unsigned i = sid - first_sid;
    const __uint128_t space = (static_cast<__uint128_t>(1) << 64);

    const __uint128_t lo128 = (space * i) / d;
    const __uint128_t hi128 = (space * (i + 1)) / d - 1;

    const uint32_t port32 = static_cast<uint32_t>(g_node_cfg.base_port) + sid;

    v.reserve(1);
    v.push_back(hash_range{static_cast<uint64_t>(lo128),
                           static_cast<uint64_t>(hi128), sid,
                           static_cast<uint16_t>(port32)});
    seastar::sstring json;

    json += "{";
    json +=
        format("\"epoch\":{},\"smp\":{},\"base_port\":{},\"port_"
               "offset\":{},\"hash\":\"{}\",\"current_shard\":{},\"ranges\":[",
               g_node_cfg.epoch, g_node_cfg.smp, g_node_cfg.base_port,
               g_node_cfg.port_offset, g_node_cfg.hash, v[0].shard);

    for (size_t i = 0; i < v.size(); ++i) {
        const auto &r = v[i];
        json += format("[\"{}\",\"{}\",{},{}]", r.lo, r.hi, r.shard, r.port);
        if (i + 1 < v.size())
            json += ",";
    }
    json += "]}";
    return json;
}

uint16_t parse_u16(const seastar::sstring &s) {
    if (s.empty()) {
        throw std::runtime_error("invalid shard id: empty");
    }

    for (char c : s) {
        if (c < '0' || c > '9') {
            throw std::runtime_error("invalid shard id: " +
                                     std::string(s.data(), s.size()));
        }
    }

    uint32_t out = 0;
    auto *begin = s.data();
    auto *end = s.data() + s.size();

    const auto [ptr, ec] = std::from_chars(begin, end, out);
    if (ec != std::errc{} || ptr != end ||
        out > std::numeric_limits<uint16_t>::max()) {
        throw std::runtime_error("invalid shard id: " +
                                 std::string(s.data(), s.size()));
    }
    return static_cast<uint16_t>(out);
}

} // namespace shunyakv
