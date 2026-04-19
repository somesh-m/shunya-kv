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
                .trigger = 0,
                .stop = 0,
                .budget = 0,
                .throttle = false,
            },
        .prob_evict_ =
            {
                .trigger = 0,
                .budget_percent = 0,
            },
    };

    pool::PoolConfig pool{
        .memory_reserve_percentage = 0.15,
        .pool_max_memory_percent = 0,
        .page_size_goal = 0,
        .key_reserve = 0,
        .prob_pool_size_percent = 0,
    };

    uint16_t db_port{60110};
    seastar::sstring hash{"fnv1a64"};
    bool send_shard_details_on_connect{false};
};
