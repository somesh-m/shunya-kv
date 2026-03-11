#pragma once

#include <cstddef>
#include <cstdint>

namespace shunyakv {

struct shard_stats_snapshot {
    uint64_t pool_fallback_allocs{0};
    std::size_t pool_available_slots{0};
    std::size_t pool_total_slots{0};
    std::size_t key_count{0};
    std::size_t eviction_count{0};
    std::size_t cache_miss{0};
};

class shard_stats {
  public:
    void record_pool_fallback_alloc() noexcept { ++pool_fallback_allocs_; }
    void record_eviction(std::size_t n = 1) { ++eviction_count_; }
    void record_cache_miss_count(std::size_t n = 1) { ++cache_miss_; }
    shard_stats_snapshot snapshot(std::size_t pool_available_slots,
                                  std::size_t pool_total_slots,
                                  std::size_t key_count) const noexcept {
        return {
            .pool_fallback_allocs = pool_fallback_allocs_,
            .pool_available_slots = pool_available_slots,
            .pool_total_slots = pool_total_slots,
            .key_count = key_count,
            .eviction_count = eviction_count_,
            .cache_miss = cache_miss_,
        };
    }

  private:
    uint64_t pool_fallback_allocs_{0};
    std::size_t eviction_count_{0};
    std::size_t cache_miss_{0};
};

} // namespace shunyakv
