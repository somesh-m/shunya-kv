#pragma once
#include "eviction/sieve_policy.hh"
#include "pool/pool.hh"
#include <cstddef>
#include <memory>
#include <seastar/core/circular_buffer.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/memory.hh>
#include <seastar/core/shard_id.hh>
#include <seastar/util/log.hh>

inline seastar::logger &pool_logger() {
    static seastar::logger logger{"object_pool"};
    return logger;
}

class CacheEntryPool {
  public:
    explicit CacheEntryPool(std::size_t max_size) : max_size_(max_size) {}

    seastar::future<>
    init(std::size_t usable_memory = 0,
         double pool_max_memory_percent = 0.6) { // call this after construction
        if (initialized_) {
            co_return;
        }
        usable_memory_ = usable_memory;
        pool_max_memory_percent_ = pool_max_memory_percent;

        if (max_size_ == 0) {
            max_size_ = calculate_optimal_pool_size();
        } else {
            // User provided max size. Verify that it fits into the allowed
            // pool memory budget now that runtime memory information is known.
            const std::size_t estimated_per_entry_size =
                get_per_entry_size_estimate();
            const std::size_t total_mem_req =
                estimated_per_entry_size * max_size_;
            if (total_mem_req > get_max_allowed_memory_for_pool()) {
                const std::size_t requested_max_size = max_size_;
                max_size_ = calculate_optimal_pool_size();
                pool_logger().info("Pool size overflow. Requested {} Feasible "
                                   "{}. Falling back to max entry possible",
                                   requested_max_size, max_size_);
            }
        }
        initialized_ = true;
        co_await prepopulate_pool();
    }

    seastar::future<std::unique_ptr<ttl::Entry>> acquire();
    void release(std::unique_ptr<ttl::Entry> entry);

    std::size_t calculate_optimal_pool_size() noexcept;
    std::size_t get_available_slots() const;
    std::size_t get_total_slots() const;
    std::size_t get_per_entry_size_estimate();
    std::size_t get_max_allowed_memory_for_pool();
    std::size_t get_used_slots() const;

  private:
    seastar::circular_buffer<std::unique_ptr<ttl::Entry>> pool_;
    std::size_t value_offset_ = 65432;
    std::size_t free_after_pool_{0};
    double pool_max_memory_percent_ = 0.7;
    std::size_t usable_memory_{0};
    std::size_t max_size_{0};
    bool initialized_ = false;
    std::shared_ptr<SievePolicy> policy_;
    seastar::future<> prepopulate_pool();
};
