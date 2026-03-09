#include "eviction/sieve_policy.hh"
#include "ttl/entry.hh"
#include <seastar/util/log.hh>

static seastar::logger sieve_logger{"sieve_policy"};

void SievePolicy::on_insert(ttl::Entry &e) {
    e.visited = true;

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

seastar::future<std::vector<seastar::sstring>> SievePolicy::evict() {
    std::vector<seastar::sstring> victim_list;
    sieve_logger.info("eviction budget {}", evCfg_.eviction_budget);
    if (sieveList_.empty()) {
        co_return victim_list;
    }

    if (hand_ == sieveList_.end()) {
        hand_ = sieveList_.begin();
    }

    std::size_t evicted_count = 0;

    while (evicted_count < evCfg_.eviction_budget &&
           hand_ != sieveList_.end()) {

        ttl::Entry &cur = *hand_;
        if (cur.visited) {
            cur.visited = false;
            ++hand_;
            continue;
        }

        auto victim_it = hand_;
        ++hand_;
        victim_list.push_back(victim_it->key);
        sieveList_.erase(victim_it);
        if (sieveList_.empty()) {
            hand_ = sieveList_.end();
            break;
        }

        ++evicted_count;
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
