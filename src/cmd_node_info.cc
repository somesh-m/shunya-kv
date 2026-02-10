#include "cmd_node_info.hh"
#include "commands.hh"

#include <resp/resp_types.hh>
#include <resp/resp_writer.hh>
#include <seastar/core/print.hh>

namespace shunyakv {

node_cfg g_node_cfg{}; // single definition

void set_node_cfg(const node_cfg &c) { g_node_cfg = c; }

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

        const uint32_t port32 =
            static_cast<uint32_t>(g_node_cfg.base_port) + sid;
        // TODO: validate port32 <= 65535 if you want strictness

        v.push_back(hash_range{static_cast<uint64_t>(lo128),
                               static_cast<uint64_t>(hi128), sid,
                               static_cast<uint16_t>(port32)});
    }
    return v;
}

static bool ieq(const seastar::sstring &a, const char *b) {
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

seastar::future<> handle_node_info(const resp::Array &cmd,
                                   seastar::output_stream<char> &out,
                                   shunyakv::service & /*svc*/) {
    using seastar::format;

    // Accept:
    //  NODE_INFO               -> minimal
    //  NODE_INFO RANGES|MAP    -> include ranges
    if (cmd.size() != 1 && cmd.size() != 2) {
        co_await resp::write_error(
            out, "ERR wrong number of arguments for 'NODE_INFO'");
        co_return;
    }

    bool want_ranges = false;
    if (cmd.size() == 2) {
        // Treat any second arg "RANGES" or "MAP" as request for ranges
        if (ieq(cmd[1], "RANGES") || ieq(cmd[1], "MAP")) {
            want_ranges = true;
        } else {
            co_await resp::write_error(out, "ERR syntax error");
            co_return;
        }
    }

    seastar::sstring json;

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

    // Reply as RESP bulk string
    co_await resp::write_bulk(out, json);
    co_return;
}

} // namespace shunyakv
