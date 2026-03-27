#pragma once
#include <cstddef>

namespace pool {
struct PoolConfig {
    enum class Mode { Fixed, ByMemory };
    Mode mode = Mode::ByMemory;
    double memory_reserve_percentage = 0.15;
    double pool_max_memory_percent = 0.7;
    size_t page_size_goal = 8192; // 8KB
    size_t key_reserve = 24;
    double prob_pool_size_percent = 0;
};
} // namespace pool
