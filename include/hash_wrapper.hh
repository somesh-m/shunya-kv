// hash_wrapper.hh
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" {
#include "fnv.h"
}

namespace shunyakv {

inline uint64_t fnv1a64(std::string_view s) noexcept {
    // Pick the right init constant name used by the upstream header
#if defined(FNV1A_64_INIT)
    constexpr Fnv64_t init = FNV1A_64_INIT;
#elif defined(FNV1_64A_INIT)
    constexpr Fnv64_t init = FNV1_64A_INIT;
#else
#error "FNV init constant not found (expect FNV1A_64_INIT or FNV1_64A_INIT)"
#endif

    // Upstream takes void*; our data() is const. Cast away const for the C API.
    // The function does not write to the buffer.
    void *ptr = const_cast<void *>(static_cast<const void *>(s.data()));
    return static_cast<uint64_t>(fnv_64a_buf(ptr, s.size(), init));
}

// Optional helpers you already had:
inline unsigned owner_shard(uint64_t h, unsigned smp, unsigned first) noexcept {
    if (smp <= first)
        return first;
    const unsigned D = smp - first;
    return first + unsigned(((__uint128_t)h * D) >> 64);
}
inline unsigned owner_shard(std::string_view key, unsigned smp,
                            unsigned first) noexcept {
    return owner_shard(fnv1a64(key), smp, first);
}

} // namespace shunyakv
