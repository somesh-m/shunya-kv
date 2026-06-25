#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <dbconfig.hh>
#include <ttl/entry.hh>
#include <seastar/core/memory.hh>
#include <seastar/util/log.hh>

namespace pool {
inline seastar::logger &manager_logger() {
    static seastar::logger logger{"memory_manager"};
    return logger;
}

class ShardMemoryManager {
  public:
    explicit ShardMemoryManager(const db_config &cfg) {
        cfg_ = cfg;
        auto stats = seastar::memory::stats();
        usable_memory_ = (1 - (cfg_.pool.memory_reserve_percentage / 100)) *
                         stats.total_memory();
        value_offset_ = cfg.pool.page_size_goal + cfg.pool.key_reserve;
        pool_max_memory_ =
            (cfg_.pool.pool_max_memory_percent / 100.0) * usable_memory_;
        const std::size_t minimum_entry_size = sizeof(ttl::Entry) + value_offset_;
        if (pool_max_memory_ > 0 && pool_max_memory_ < minimum_entry_size) {
            pool_max_memory_ = minimum_entry_size;
        }
        manager_logger().info("ALL SHARD MAX USABLE MEMORY: {}",
                              usable_memory_);
        manager_logger().info("ALL SHARD POOL MAX MEMORY: {}",
                              pool_max_memory_);
    }

    bool try_reserve(std::size_t bytes) {
        if (used_ + bytes > pool_max_memory_) {
            return false;
        }

        used_ += bytes;
        return true;
    }

    void release(std::size_t bytes) { used_ -= std::min(used_, bytes); }

    std::size_t used() const { return used_; }
    std::size_t hard_limit() const { return pool_max_memory_; }
    std::size_t get_value_offset() const { return value_offset_; }

  private:
    std::size_t used_{0};
    std::size_t value_offset_ = 65432;
    double pool_max_memory_{0};
    db_config cfg_;

    // Bookkeeping
    std::size_t usable_memory_{0};
};
} // namespace pool
