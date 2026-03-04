#pragma once

#include <boost/intrusive/list_hook.hpp>
#include <cstdint>
#include <seastar/core/sstring.hh>
#include <string>

class CacheEntryPool;

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

    bool visited = false; // used for sieve eviction

    // This adds the next/prev pointers directly inside the object
    bi::list_member_hook<bi::link_mode<bi::safe_link>> list_hook;

    // provide a function to update the value
    void update_from(Entry &&updated_object, bool update_ttl) {
        this->value = std::move(updated_object.value);
        if (update_ttl) {
            this->expires_at = std::move(updated_object.expires_at);
            this->ver += 1;
        }
    }

    // public getter for in_use
  public:
    bool is_in_use() const noexcept { return in_use_; }

  private:
    bool in_use_ = false;

    friend class ::CacheEntryPool;
};
} // namespace ttl
