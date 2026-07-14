#pragma once
#include "pool/shard_memory_manager.hh"
#include "pool/vector_pool_traits.hh"
#include "vector/vector_entry.hh"
#include <algorithm>
#include <cstddef>
#include <dbconfig.hh>
#include <memory>
#include <seastar/core/circular_buffer.hh>
#include <seastar/core/future.hh>
#include <seastar/util/log.hh>

inline seastar::logger &vector_logger() {
    static seastar::logger logger{"vector_pool"};
    return logger;
}

class EntryPool {
  public:
    EntryPool(pool::ShardMemoryManager &memory_manager, const db_config &cfg,
              std::size_t initial_count, std::size_t growth_count)
        : memory_manager_(memory_manager), cfg_(cfg),
          initial_count_(initial_count), growth_count_(growth_count),
          free_slots_low_watermark_(
              std::max<std::size_t>(1, growth_count / 10)) {}

    seastar::future<> init();
    void release(std::unique_ptr<shunyakv::vdb::Entry> entry);
    seastar::future<std::unique_ptr<shunyakv::vdb::Entry>> acquire();

    std::size_t available() const noexcept { return pool_.size(); }
    std::size_t total_allocated() const noexcept { return total_allocated_; }
    std::size_t used() const noexcept {
        return total_allocated_ - pool_.size();
    }

  private:
    seastar::future<> grow(std::size_t count);

    pool::ShardMemoryManager &memory_manager_;
    const db_config &cfg_;
    seastar::circular_buffer<std::unique_ptr<shunyakv::vdb::Entry>> pool_;
    std::size_t initial_count_ = 0;
    std::size_t growth_count_ = 0;
    std::size_t free_slots_low_watermark_ = 1;
    std::size_t total_allocated_ = 0;
    bool initialized_ = false;
};
