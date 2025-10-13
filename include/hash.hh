// hash.hh
#pragma once
#include <cstdint>
#include <seastar/core/smp.hh>
#include <string_view>

namespace shunyakv {
inline uint64_t fnv1a64(std::string_view s) noexcept {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

// map 64-bit hash -> [0, smp)
inline unsigned shard_for(std::string_view key) noexcept {
    const uint64_t h = fnv1a64(key);
    const uint64_t n = seastar::smp::count;
    return unsigned(((__uint128_t)h * n) >> 64); // multiply-high scaling
}
} // namespace shunyakv
