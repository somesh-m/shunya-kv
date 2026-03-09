#pragma once
#include <cstddef>
#include <cstdint>
#include <eviction/eviction_config.hh>
#include <seastar/core/sstring.hh>

struct db_config {
    eviction::EvictionConfig ev_config{
        .policy = eviction::EvictionPolicy::Sieve,
        .soft_ =
            {
                .trigger = 0.6,
                .stop = 0.5,
                .budget = 1000,
                .throttle = false,
            },
        .hard_ =
            {
                .trigger = 0.8,
                .stop = 0.7,
                .budget = 2500,
                .throttle = true,
            },
    };

    uint16_t db_port{60110};
    seastar::sstring hash{"fnv1a64"};
    bool send_shard_details_on_connect{false};
    size_t page_size_goal = 8192; // 8KB
    size_t key_reserve = 24;
};
