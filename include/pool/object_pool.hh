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
    explicit CacheEntryPool(std::size_t max_size)
        : max_size_(max_size == 0 ? calculate_optimal_pool_size() : max_size) {
        if (max_size != 0) {
            // user provided max size. we need to verify that the number of
            // entry will fit into the available memory, it it overflows, bring
            // it down to the max that will be supported by the available
            // memory.
            std::size_t estimated_per_entry_size =
                get_per_entry_size_estimate();
            std::size_t total_mem_req = estimated_per_entry_size * max_size;

            if (total_mem_req > get_usable_memory()) {
                max_size_ = calculate_optimal_pool_size();
                pool_logger().info("Pool size overflow. Requested {} Feasible "
                                   "{}. Falling back to max entry possible",
                                   max_size, max_size_);
            }

            pool_logger().info("Allocated max pool {}", max_size_);
        }
    }

    seastar::future<> init() { // call this after construction
        if (initialized_) {
            co_return;
        }
        initialized_ = true;
        co_await prepopulate_pool();
    }

    seastar::future<std::unique_ptr<ttl::Entry>> acquire();
    void release(std::unique_ptr<ttl::Entry> entry);

    std::size_t calculate_optimal_pool_size() noexcept;
    std::size_t get_available_slots();
    std::size_t get_total_slots();
    std::size_t get_per_entry_size_estimate();
    std::size_t get_usable_memory();

  private:
    seastar::circular_buffer<std::unique_ptr<ttl::Entry>> pool_;
    std::size_t value_offset_ = 65536;
    double allowed_percentage_total_mem_ = 0.8;
    std::size_t max_size_ = 1024;
    bool initialized_ = false;
    std::shared_ptr<SievePolicy> policy_;
    seastar::future<> prepopulate_pool();
};
