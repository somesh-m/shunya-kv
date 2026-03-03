#pragma once
#include <cstdint>
#include <seastar/core/sstring.hh>

struct db_config {
    uint16_t db_port{60110};
    seastar::sstring hash{"fnv1a64"};
    bool send_shard_details_on_connect{false};
    seastar::sstring policy;
    uint64_t eviction_trigger_cutoff;
    uint64_t eviction_stop_cutoff;
    uint64_t eviction_budget;
};
