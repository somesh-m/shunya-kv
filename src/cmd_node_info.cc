#include "cmd_node_info.hh"
#include "commands.hh"

#include <cstring>
#include <resp/resp_types.hh>
#include <resp/resp_writer.hh>
#include <seastar/core/print.hh>
namespace shunyakv {

node_cfg g_node_cfg{}; // single definition

void set_node_cfg(const node_cfg &c) { g_node_cfg = c; }

static bool ieq(const std::string_view &a, const char *b) {
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

seastar::future<> handle_node_info(const resp::ArgvView &cmd,
                                   seastar::output_stream<char> &out,
                                   shunyakv::service & /*svc*/) {

    // Accept:
    //  NODE_INFO               -> minimal
    //  NODE_INFO RANGES|MAP    -> include ranges
    if (cmd.size() != 1 && cmd.size() != 2) {
        co_await resp::write_error(
            out, "ERR wrong number of arguments for 'NODE_INFO'");
        co_return;
    }
    bool want_ranges = false;
    seastar::sstring json;
    if (cmd.size() == 2 && (ieq(cmd[1], "RANGES") || ieq(cmd[1], "MAP"))) {
        json = compute_hash_ranges();
        co_await resp::write_bulk(out, json);
        co_return;
    }
    if (cmd.size() == 2 && ieq(cmd[1], "SHARDS")) {
        co_await resp::write_bulk(out, get_smp_count());
        co_return;
    }
    uint16_t shard_no = static_cast<uint16_t>(seastar::this_shard_id());
    if (cmd.size() == 2) {
        shard_no = parse_u16(cmd[1]);
    }
    json = compute_hash(static_cast<uint16_t>(shard_no));
    co_await resp::write_bulk(out, json);
    co_return;
}

} // namespace shunyakv
