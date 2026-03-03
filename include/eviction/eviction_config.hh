#pragma once
#include <cstddef>
#include <cstdint>
namespace eviction {
enum class PolicyKind { Sieve };

struct EvictionConfig {
    PolicyKind policy{PolicyKind::Sieve};
    double eviction_trigger_cutoff;
    double eviction_stop_cutoff;
    std::size_t eviction_budget;
};
} // namespace eviction
