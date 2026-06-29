#pragma once
#include "pool/pool.hh"
#include "pool/shard_memory_manager.hh"
#include "pool/vector_pool_traits.hh"
#include "vector/vector_entry.hh"
#include <cstddef>
#include <dbconfig.hh>
#include <memory>
#include <optional>
#include <pool/object_pool_template.hh>
#include <seastar/core/circular_buffer.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/memory.hh>
#include <seastar/core/shard_id.hh>
#include <seastar/util/log.hh>

inline seastar::logger &vector_logger() {
    static seastar::logger logger{"vector_pool"};
    return logger;
}

class EntryPool {
  public:
    EntryPool(pool::ShardMemoryManager &memory_manager, const db_config &cfg,
              std::size_t initial_count, std::size_t growth_count)
        : pool_(memory_manager, cfg, initial_count, growth_count), cfg_(cfg) {}

    void release(std::unique_ptr<shunyakv::vdb::Entry> entry);
    seastar::future<std::unique_ptr<shunyakv::vdb::Entry>> acquire();

  private:
    ObjectPool<shunyakv::vdb::Entry> pool_;
    std::size_t value_offset_ =
        65432; // Will be overwritten by the value from config
    const db_config &cfg_;
};
