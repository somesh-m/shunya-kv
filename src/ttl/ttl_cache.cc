#include "ttl/ttl_cache.hh"

namespace ttl {
TtlCache::TtlCache(const ttl_policy *policy) : policy_(policy) {}

void TtlCache::set(const std::string &key, std::string value, uint64_t now,
                   uint64_t ttl) {
    Entry &e = kv_[key];
    e.value = std::move(value);
    e.expires_at = now + ttl;
    e.ver++;
    e.last_access = now;

    pq_.push(HeapNode{e.expires_at, key, e.ver});
}

std::optional<std::string> TtlCache::get(const std::string &key, uint64_t now) {
    auto it = kv_.find(key);
    if (it == kv_.end())
        return std::nullopt;

    Entry &e = it->second;

    // If expired, erase immediately (fast cleanup on read)
    if (is_expired(now, e.expires_at)) {
        kv_.erase(it);
        return std::nullopt;
    }

    // reward extension
    if (policy_ != nullptr) {
        bool should_extend = true;
        if (extend_threshold_ != 0) {
            uint64_t remaining =
                (e.expires_at > now) ? (e.expires_at - now) : 0;
            should_extend = remaining < extend_threshold_;
        }

        if (should_extend) {
            e.expires_at = policy_->compute_new_expiry(now, e);
            e.ver++;
            pq_.push(HeapNode{e.expires_at, key, e.ver});
        } else {
            e.last_access = now;
        }
    } else {
        e.last_access = now;
    }

    return e.value;
}

std::size_t TtlCache::evict(uint64_t now, std::size_t budget) {
    std::size_t removed = 0;

    while (budget-- > 0 && !pq_.empty()) {
        const HeapNode top = pq_.top();
        if (!is_expired(now, top.expires_at))
            break;

        pq_.pop();

        auto it = kv_.find(top.key);
        if (it == kv_.end())
            continue;

        Entry &e = it->second;

        // Lazy invalidation checks
        if (e.ver != top.ver)
            continue;
        if (e.expires_at != top.expires_at)
            continue;

        kv_.erase(it);
        removed++;
    }
    return removed;
}
bool TtlCache::del(const std::string &key) { return kv_.erase(key) > 0; }
} // namespace ttl
