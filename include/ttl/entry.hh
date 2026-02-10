#pragma once

#include <cstdint>
#include <string>

namespace ttl {
struct Entry {
    std::string value;

    // Absolute expiration time
    uint64_t expires_at = 0;

    // Version for lazy heap invalidation
    uint32_t ver = 0;

    // Heat / tier for adaptive TTL
    uint8_t heat = 0;

    // Last access time (for idle reset)
    uint64_t last_access = 0;
};
} // namespace ttl
