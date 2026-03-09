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
    bool throttle;
};

struct EvictionConfig {
    EvictionPolicy policy{EvictionPolicy::Sieve};
    EvictConfig soft_;
    EvictConfig hard_;
};
} // namespace eviction
