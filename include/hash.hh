// hash.hh
#pragma once
#include "hash_wrapper.hh"
#include <cstdint>
#include <seastar/core/smp.hh>
#include <string_view>

namespace shunyakv {

// -------- Config knob: which shard index is the first *data* shard --------
// Default = 0 (legacy: all shards 0..smp-1 are data). Set to 1 if shard 0 is
// admin-only.
inline unsigned g_first_data_shard = 0;

// Call this once at startup (e.g., in main on shard 0) if shard 0 is
// admin-only:
inline void set_first_data_shard(unsigned first) noexcept {
    g_first_data_shard = first;
}

// -------- Legacy-compatible API: shard_for(key) --------
// Now maps key into [g_first_data_shard .. seastar::smp::count-1]
inline unsigned shard_for(std::string_view key) noexcept {
    const unsigned smp = seastar::smp::count;
    const unsigned first = g_first_data_shard;
    if (smp <= first) [[unlikely]] {
        return first; // degenerate: no data shards configured
    }
    const unsigned D = smp - first; // number of data shards
    const uint64_t h = shunyakv::fnv1a64(key);
    const unsigned idx = unsigned(((__uint128_t)h * D) >> 64); // [0..D)
    return first + idx;                                        // shard id
}

// Optional: bytes overload
inline unsigned shard_for(const void *p, size_t n) noexcept {
    return shard_for(std::string_view{static_cast<const char *>(p), n});
}

} // namespace shunyakv
