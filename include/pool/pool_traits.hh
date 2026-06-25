#pragma once

#include "pool/shard_memory_manager.hh"

template <typename T> struct PoolTraits {
    static std::size_t estimated_size(const db_config &) { return sizeof(T); }

    static void initialize(T &, pool::ShardMemoryManager &) {}

    static void reset(T &obj, pool::ShardMemoryManager &) { obj = T{}; }
};
