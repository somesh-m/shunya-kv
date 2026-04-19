#pragma once
#include <cstddef>
#include <cstdint>
namespace eviction {
enum class EvictionPolicy { Sieve };
enum class EvictionType { soft, hard };
struct EvictConfig {
    double trigger;
    double stop;
    uint64_t budget;
    double budget_percent = 0.0; // Used only in case of probation pool
    bool throttle;
};

struct EvictionConfig {
    EvictionPolicy policy{EvictionPolicy::Sieve};
    EvictConfig soft_;
    EvictConfig prob_evict_; // Used for evicting from probation pool
};
} // namespace eviction
