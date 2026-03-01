#include "ttl/ttl_cache.hh"
#include <new>

namespace ttl {
TtlCache::TtlCache(const ttl_policy *policy, uint32_t owner_id)
    : provider_(sizeof(Entry), 64 * 1024, owner_id), policy_(policy) {}

TtlCache::~TtlCache() { clear_entries(); }

Entry *TtlCache::allocate_entry() {
    MemoryId mem;
    if (!provider_.try_fetch_entry(mem)) {
        return nullptr;
    }
    if (mem.size < sizeof(Entry)) {
        provider_.deallocate(mem.base);
        return nullptr;
    }
    return new (mem.base) Entry();
}

void TtlCache::release_entry(Entry *e) noexcept {
    if (!e) {
        return;
    }
    e->~Entry();
    provider_.deallocate(reinterpret_cast<std::byte *>(e));
}

void TtlCache::clear_entries() noexcept {
    for (auto &[key, entry] : kv_) {
        (void)key;
        release_entry(entry);
    }
    kv_.clear();
}

void TtlCache::set(const std::string &key, std::string value, uint64_t now,
                   uint64_t ttl) {
    auto it = kv_.find(key);
    if (it == kv_.end()) {
        Entry *entry = allocate_entry();
        if (!entry) {
            return;
        }
        auto [inserted_it, inserted] = kv_.emplace(key, entry);
        if (!inserted) {
            release_entry(entry);
            it = inserted_it;
        } else {
            it = inserted_it;
        }
    } else if (it->second == nullptr) {
        Entry *entry = allocate_entry();
        if (!entry) {
            return;
        }
        it->second = entry;
    }

    Entry &e = *(it->second);
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

    Entry *ep = it->second;
    if (!ep) {
        kv_.erase(it);
        return std::nullopt;
    }
    Entry &e = *ep;

    // If expired, erase immediately (fast cleanup on read)
    if (is_expired(now, e.expires_at)) {
        release_entry(ep);
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

        Entry *ep = it->second;
        if (!ep) {
            kv_.erase(it);
            continue;
        }
        Entry &e = *ep;

        // Lazy invalidation checks
        if (e.ver != top.ver)
            continue;
        if (e.expires_at != top.expires_at)
            continue;

        release_entry(ep);
        kv_.erase(it);
        removed++;
    }
    return removed;
}

bool TtlCache::del(const std::string &key) {
    auto it = kv_.find(key);
    if (it == kv_.end()) {
        return false;
    }
    release_entry(it->second);
    kv_.erase(it);
    return true;
}
} // namespace ttl
