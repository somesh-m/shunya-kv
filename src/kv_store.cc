#include "kv_store.hh"
#include "hotpath_metrics.hh"
#include "proto_helpers.cc"
#include "ttl/entry.hh"
#include <chrono>
#include <coroutine>
#include <memory>
#include <seastar/core/coroutine.hh>
#include <seastar/core/memory.hh>
#include <seastar/core/print.hh>
#include <seastar/core/smp.hh>
#include <seastar/util/log.hh>

using Clock = std::chrono::steady_clock;
namespace shunyakv {

static seastar::logger kv_store_log{"kv_store"};

namespace {
inline bool
insert_with_growth_trace(absl::flat_hash_map<key_t, seastar::sstring> &map,
                         key_t key, seastar::sstring value) {
    const auto before_size = map.size();
    const auto before_buckets = map.bucket_count();
    const auto t0 = std::chrono::steady_clock::now();

    map[std::move(key)] = std::move(value);

    const auto after_buckets = map.bucket_count();
    const auto dt_us = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now() - t0)
                           .count();

    if (after_buckets != before_buckets) {
        kv_store_log.warn("map growth on shard {}: size {} -> {}, buckets {} "
                          "-> {}, insert took {} us",
                          seastar::this_shard_id(), before_size, map.size(),
                          before_buckets, after_buckets, dt_us);
    } else if (dt_us >= 2000) {
        kv_store_log.info(
            "slow map insert on shard {}: size={}, buckets={}, took {} us",
            seastar::this_shard_id(), map.size(), after_buckets, dt_us);
    }
    return true;
}
} // namespace

seastar::future<> store::start(unsigned) {
    /**
     * Reserve a large amount here so that it doesn't reallocate while the db is
     * running.
     */
    _map.reserve(27000'00);

    entry_pool_ = CacheEntryPool(0);
    if (!sieve_policy_.has_value()) {
        eviction::EvictionConfig ev_cfg{
            .policy = eviction::PolicyKind::Sieve,
            .eviction_trigger_cutoff = 0.8,
            .eviction_stop_cutoff = 0.7,
            .eviction_budget = 512,
        };
        sieve_policy_.emplace(ev_cfg);
    }

    ttl_policy_ = ttl::TtlCache{};
    co_return;
}

seastar::future<> store::stop() {
    _map.clear();
    absl::flat_hash_map<key_t, ttl::Entry>().swap(_map);
    _map.rehash(0);
    co_return;
}

seastar::future<bool> store::set(key_t key, seastar::sstring value) {
    auto pooled_entry = co_await entry_pool_.acquire();
    if (!pooled_entry) {
        co_return false;
    }

    pooled_entry->value = std::move(value);
    pooled_entry->expires_at = 0;

    _map[std::move(key)] = std::move(pooled_entry);
    sieve_policy_.on_insert(pooled_entry);
    co_await check_memory_and_evict();
    co_return true;
}

seastar::future<bool> store::set_with_ttl(key_t key, seastar::sstring value,
                                          uint64_t ttl) {
    auto pooled_entry = co_await entry_pool_.acquire();
    if (!pooled_entry) {
        co_return false;
    }

    pooled_entry->value = std::move(value);
    pooled_entry->expires_at = now_s() + ttl;

    _map[std::move(key)] = std::move(pooled_entry);
    sieve_policy_.on_insert(pooled_entry);
    co_await check_memory_and_evict();

    co_return true;
}

seastar::future<std::optional<seastar::sstring>>
store::get(std::string_view key) const {
    auto it = _map.find(key_t{key.data(), key.size()});
    if (it == _map.end()) {
        co_return std::nullopt;
    }
    const ttl::Entry &e = it->second;
    if (e.expires_at != 0 && now_s() >= e.expires_at) {
        sieve_policy_.on_erase(e);
        entry_pool_.release(e);
        co_return std::nullopt;
    }
    sieve_policy_.on_hit(e);
    co_return std::optional<seastar::sstring>(e.value);
}

seastar::future<> store::check_memory_and_evict() {
    // 1. Get memory statistics for the current shard
    auto stats = seastar::memory::stats();

    // total_memory() is the maximum allowed for this shard
    // allocated_memory() is what is currently used
    size_t currently_used = stats.allocated_memory();
    size_t limit = stats.total_memory();

    // 2. Calculate percentage
    double usage_fraction = static_cast<double>(currently_used) / limit;

    // 3. Trigger eviction if over threshold (e.g., 80%)
    if (usage_fraction > 0.8) {
        if (!sieve_policy_.has_value()) {
            co_return;
        }

        // TODO: the below two evict returns std::vector(Entry *). run through
        // the loops and remove the Entry and return back to pool.
        const auto sieve_victims = co_await sieve_policy_->evict();
        const auto ttl_victims = co_await ttl_policy_.evict(now_s(), 200);

        auto reclaim_entry = [this](ttl::Entry *victim) {
            if (!victim) {
                return;
            }
            for (auto it = _map.begin(); it != _map.end(); ++it) {
                if (&it->second == victim) {
                    auto pooled = std::make_unique<ttl::Entry>(
                        std::move(it->second));
                    _map.erase(it);
                    entry_pool_.release(std::move(pooled));
                    break;
                }
            }
        };

        for (ttl::Entry *victim : sieve_victims) {
            reclaim_entry(victim);
        }
        for (ttl::Entry &victim : ttl_victims) {
            reclaim_entry(&victim);
        }

        seastar::print("Memory high ({:.2f}%), triggering eviction\n",
                       usage_fraction);
    }
    co_return;
}

} // namespace shunyakv
