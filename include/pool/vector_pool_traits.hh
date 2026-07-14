#pragma once

#include "pool/pool_traits.hh"
#include "pool/shard_memory_manager.hh"
#include "vector/vector_entry.hh"

template <> struct PoolTraits<shunyakv::vdb::Entry> {

    static std::size_t value_offset(const db_config &cfg) {
        return cfg.pool.page_size_goal + cfg.pool.key_reserve;
    }

    static std::size_t estimated_size(const db_config &cfg) {
        return sizeof(shunyakv::vdb::Entry) + value_offset(cfg);
    }

    static void initialize(shunyakv::vdb::Entry &entry,
                           const db_config &cfg) {
        entry.value.reserve(value_offset(cfg));
    }

    static void reset(shunyakv::vdb::Entry &entry,
                      const db_config &cfg) {
        entry.in_use_ = false;
        entry.visited = false;
        entry.value.clear();
        entry.key = "";
        entry.embedding.clear();
        entry.ver = 0;
        entry.centroid = 0;

        const std::size_t offset = value_offset(cfg);
        if (entry.value.capacity() > offset * 2) {
            std::string tmp;
            tmp.reserve(offset);
            entry.value.swap(tmp);
        }
    }
};
