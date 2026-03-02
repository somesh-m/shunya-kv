#pragma once
#include <cstddef>
#include <cstdint>
namespace eviction {
enum class PolicyKind { Sieve };

struct EvictionConfig {
    PolicyKind policy{PolicyKind::Sieve};
    std::size_t eviction_trigger_cutoff;
    std::size_t eviction_stop_cutoff;
    std::size_t eviction_budget;
};
} // namespace eviction
