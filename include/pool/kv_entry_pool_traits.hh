#pragma once

#include "pool/pool_traits.hh"
#include "pool/shard_memory_manager.hh"
#include "ttl/entry.hh"

template <> struct PoolTraits<ttl::Entry> {

    static std::size_t value_offset(const db_config &cfg) {
        return cfg.pool.page_size_goal + cfg.pool.key_reserve;
    }

    static std::size_t estimated_size(const db_config &cfg) {
        return sizeof(ttl::Entry) + value_offset(cfg);
    }

    static void initialize(ttl::Entry &entry, pool::ShardMemoryManager &memory) {
        entry.value.reserve(memory.get_value_offset());
    }

    static void reset(ttl::Entry &entry, pool::ShardMemoryManager &memory) {
        // KV-specific reset

        entry.in_use_ = false;
        entry.visited = false;
        entry.value.clear();
        entry.key = "";
        entry.expires_at = 0;
        entry.ver = 0;
        entry.heat = 0;
        entry.last_access = 0;
        entry.pool_type = ttl::PoolType::Probation;

        const std::size_t offset = memory.get_value_offset();
        if (entry.value.capacity() > offset * 2) {
            std::string tmp;
            tmp.reserve(offset);
            entry.value.swap(tmp);
        }
    }
};
