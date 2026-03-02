#pragma once
#include "eviction/sieve_policy.hh"
#include "pool/pool.hh"
#include <cstddef>
#include <memory>
#include <seastar/core/circular_buffer.hh>
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
        prepopulate_pool();
    }

    seastar::future<std::unique_ptr<ttl::Entry>> acquire();
    void release(std::unique_ptr<ttl::Entry> entry);

    void prepopulate_pool();

    std::size_t calculate_optimal_pool_size() noexcept;
    std::size_t get_available_slots();
    std::size_t get_total_slots();

  private:
    seastar::circular_buffer<std::unique_ptr<ttl::Entry>> pool_;
    std::size_t max_size_;
    std::shared_ptr<SievePolicy> policy_;
};
