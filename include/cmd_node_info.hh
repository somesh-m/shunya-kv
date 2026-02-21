#pragma once
#include "commands.hh"
#include <resp/resp_types.hh>

namespace shunyakv {
seastar::future<> handle_node_info(const resp::ArgvView &cmd,
                                   seastar::output_stream<char> &out,
                                   shunyakv::service &svc);

struct node_cfg {
    uint16_t base_port{60110};
    unsigned smp{1};
    unsigned port_offset{1};
    seastar::sstring hash{"fnv1a435345"};
    uint64_t epoch{1};
    bool isPreHashed{false};
};
inline node_cfg g_cfg;

struct hash_range {
    uint64_t lo; // inclusive
    uint64_t hi; // inclusive
    unsigned shard;
    unsigned port;
};

extern node_cfg g_node_cfg;

void set_node_cfg(const node_cfg &);
// Equal contiguous ranges over the 64-bit hash space for *data* shards
seastar::sstring compute_hash_ranges();
seastar::sstring compute_hash(uint16_t shard_no);
uint16_t parse_u16(const std::string_view &s);
// Utility: port for a shard id, consistent with your mapping
inline uint16_t port_for_shard(unsigned sid) {
    // data ports are base_port + (sid - first_data_shard) + 1
    const unsigned first = g_node_cfg.port_offset ? 1u : 0u;
    return static_cast<uint16_t>(g_node_cfg.base_port + (sid - first) + 1u);
}

} // namespace shunyakv
