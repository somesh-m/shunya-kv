#pragma once

#include <boost/intrusive/list_hook.hpp>
#include <cstdint>
#include <string>

namespace bi = boost::intrusive;

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

    bool in_use = false;

    bool visited = false; // used for sieve eviction

    // This adds the next/prev pointers directly inside the object
    bi::list_member_hook<bi::link_mode<bi::safe_link>> list_hook;
};
} // namespace ttl
