#include "ttl/ttl_cache.hh"

namespace ttl {

seastar::future<std::vector<seastar::sstring>>
TtlCache::evict(uint64_t now, std::size_t budget) {
    std::vector<seastar::sstring> victim_list;
    std::size_t removed = 0;

    while (budget-- > 0 && !pq_.empty()) {
        const HeapNode
        top = pq_.top();
        if (!is_expired(now, top.expires_at))
            break;

        pq_.pop();

        auto it = kv_.find(top.key);
        if (it == kv_.end())
            continue;

        Entry &e = *it->second;

        // Lazy invalidation checks
        if (e.ver != top.ver)
            continue;
        if (e.expires_at != top.expires_at)
            continue;

        victim_list.push_back(top.key);

        removed++;
        if (removed % 100 == 0) {
            co_await seastar::coroutine::maybe_yield();
        }
    }
    co_return victim_list;
}
} // namespace ttl
