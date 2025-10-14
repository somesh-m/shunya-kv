#include "cmd_node_info.hh"
#include "commands.hh"
#include "proto_helpers.hh" // split_first, trim
#include <seastar/core/print.hh>

namespace shunyakv {
node_cfg g_node_cfg{}; // single definition

void set_node_cfg(const node_cfg &c) {
    g_node_cfg = c; // copy
}

std::vector<hash_range> compute_hash_ranges() {
    std::vector<hash_range> v;

    const unsigned first_sid = g_node_cfg.port_offset ? 1u : 0u;
    const unsigned last_sid = (g_node_cfg.smp == 0 ? 0u : g_node_cfg.smp);
    if (g_node_cfg.smp == 0 || last_sid < first_sid) {
        return v;
    }

    const unsigned D = last_sid - first_sid;
    v.reserve(D);

    const __uint128_t SPACE = (__uint128_t)1 << 64;

    for (unsigned i = 0; i < D; ++i) {
        const unsigned sid = first_sid + i;

        const __uint128_t lo128 = (SPACE * i) / D;
        const __uint128_t hi128 = (SPACE * (i + 1)) / D - 1;

        // If shard 0 is admin-only (first_sid==1), data ports are base+sid.
        // If you ever allow shard 0 as data, ensure admin uses a different
        // base.
        const uint32_t port32 =
            static_cast<uint32_t>(g_node_cfg.base_port) + sid;
        if (port32 > 65535) {
            // handle error (throw/log) – port out of range
        }

        v.push_back(hash_range{static_cast<uint64_t>(lo128),
                               static_cast<uint64_t>(hi128), sid,
                               static_cast<uint16_t>(port32)});
    }
    return v;
}

seastar::future<> handle_node_info(std::string_view args,
                                   seastar::output_stream<char> &out,
                                   shunyakv::service &svc) {
    using seastar::format;

    // trim spaces in args
    auto ltrim = [](std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.remove_prefix(1);
        return s;
    };
    auto rtrim = [](std::string_view s) {
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                              s.back() == '\r' || s.back() == '\n'))
            s.remove_suffix(1);
        return s;
    };
    auto cmd = rtrim(ltrim(args));

    seastar::sstring json;

    const bool want_ranges =
        !(cmd == "NODE_INFO" || cmd == "node_info"); // default MAP

    if (want_ranges) {
        auto ranges = compute_hash_ranges();
        json += "{";
        json += format("\"epoch\":{},\"smp\":{},\"base_port\":{},\"port_"
                       "offset\":{},\"hash\":\"{}\",\"ranges\":[",
                       g_node_cfg.epoch, g_node_cfg.smp, g_node_cfg.base_port,
                       g_node_cfg.port_offset, g_node_cfg.hash);

        for (size_t i = 0; i < ranges.size(); ++i) {
            const auto &r = ranges[i];
            json += format("[{},{} , {}, {}]", r.lo, r.hi, r.shard, r.port);
            if (i + 1 < ranges.size())
                json += ",";
        }
        json += "]}";
    } else {
        json = format(
            R"({{"epoch":{},"smp":{},"base_port":{},"port_offset":{},"hash":"{}"}})",
            g_node_cfg.epoch, g_node_cfg.smp, g_node_cfg.base_port,
            g_node_cfg.port_offset, g_node_cfg.hash);
    }

    json += "\r\n";
    return out.write(json).then([&out] { return out.flush(); });
}
} // namespace shunyakv
