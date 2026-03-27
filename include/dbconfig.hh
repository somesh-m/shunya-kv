#pragma once
#include <cstddef>
#include <cstdint>
#include <eviction/eviction_config.hh>
#include <pool/pool_config.hh>
#include <seastar/core/sstring.hh>

struct db_config {
    eviction::EvictionConfig ev_config{
        .policy = eviction::EvictionPolicy::Sieve,
        .soft_ =
            {
                .trigger = 0.8,
                .stop = 0.7,
                .budget = 500,
                .throttle = false,
            },
        .prob_evict_ =
            {
                .trigger = 0.90,
                .budget_percent = 0.10,
            },
    };
    pool::PoolConfig pool;

    uint16_t db_port{60110};
    seastar::sstring hash{"fnv1a64"};
    bool send_shard_details_on_connect{false};
};
