#pragma once

#include <boost/intrusive/list_hook.hpp>
#include <cstdint>
#include <string>

namespace bi = boost::intrusive;

namespace ttl {
struct Entry {
    std::string value;

    /**
     * We store the key inside the entry to avoid scanning the entire map
     * during eviction to locate the associated key.
     *
     * This introduces additional memory overhead per entry, but prevents
     * an O(N) map scan per eviction, which would be extremely expensive
     * when handling large keyspaces and batch evictions.
     *
     * The tradeoff favors predictable eviction performance over minimal memory
     * usage.
     */
    seastar::sstring key;

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
