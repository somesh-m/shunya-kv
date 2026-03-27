#include "eviction/sieve_policy.hh"
#include "ttl/entry.hh"
#include <seastar/util/log.hh>

static seastar::logger sieve_logger{"sieve_policy"};

inline bool is_expired(uint64_t now, uint64_t expires_at) {
    return expires_at != 0 && now >= expires_at;
}

void SievePolicy::on_insert(ttl::Entry &e) {
    if (e.pool_type == ttl::PoolType::Sanctuary) {
        // This is already added in the sanctuary
        // on_insert could be called multiple times
        return;
    }
    e.visited = true;
    e.pool_type = ttl::PoolType::Sanctuary;

    if (!e.list_hook.is_linked()) {
        sieveList_.push_back(e);
    }

    if (hand_ == sieveList_.end()) {
        hand_ = sieveList_.begin();
    }
}

void SievePolicy::on_erase(ttl::Entry &e) {
    if (!e.list_hook.is_linked()) {
        return;
    }

    auto it = sieveList_.iterator_to(e);
    if (hand_ == it) {
        ++hand_;
        if (hand_ == sieveList_.end() && !sieveList_.empty()) {
            hand_ = sieveList_.begin();
        }
    }

    sieveList_.erase(it);
    if (sieveList_.empty()) {
        hand_ = sieveList_.end();
    }
}

seastar::future<std::vector<seastar::sstring>>
SievePolicy::evict(uint64_t now) {
    std::vector<seastar::sstring> victim_list;
    victim_list.reserve(evictParams.budget);
    if (sieveList_.empty()) {
        co_return victim_list;
    }

    if (hand_ == sieveList_.end()) {
        hand_ = sieveList_.begin();
    }

    std::size_t evicted_count = 0;

    while (evicted_count < evictParams.budget && hand_ != sieveList_.end()) {

        ttl::Entry &cur = *hand_;
        if (is_expired(now, cur.expires_at) || !cur.visited) {
            auto victim_it = hand_;
            ++hand_;
            victim_list.push_back(victim_it->key);
            sieveList_.erase(victim_it);
            if (sieveList_.empty()) {
                hand_ = sieveList_.end();
                break;
            }

            ++evicted_count;
        } else {
            cur.visited = false;
            ++hand_;
            continue;
        }

        if (evicted_count % 500 == 0) {
            co_await seastar::coroutine::maybe_yield();
        }
    }

    if (sieveList_.empty()) {
        hand_ = sieveList_.end();
    } else if (hand_ == sieveList_.end()) {
        hand_ = sieveList_.begin();
    }

    co_return victim_list;
}

void SievePolicy::on_hit(ttl::Entry &e) { e.visited = true; }

std::size_t SievePolicy::size() const noexcept { return sieveList_.size(); }

bool SievePolicy::empty() const noexcept { return sieveList_.empty(); }
