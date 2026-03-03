#include "kv_store.hh"
#include "hotpath_metrics.hh"
#include "proto_helpers.cc"
#include "ttl/entry.hh"
#include "ttl/heap_node.hh"
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
    absl::flat_hash_map<key_t, std::unique_ptr<ttl::Entry>>().swap(_map);
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
    sieve_policy_->on_insert(*pooled_entry);
    co_await check_memory_and_evict();
    co_return true;
}

seastar::future<bool> store::set_with_ttl(std::string_view key_view,
                                          seastar::sstring value,
                                          uint64_t ttl) {
    auto pooled_entry = co_await entry_pool_.acquire();
    if (!pooled_entry) {
        co_return false;
    }
    /**
     *Entry->key is of type seastar::sstring
     *We copy the key here
     *This uses seastar::sstring, which would either use COW or SSO. Both being
     *highly performant.
     */
    pooled_entry->key = seastar::sstring(key_view);
    pooled_entry->value = std::move(value);
    pooled_entry->expires_at = now_s() + ttl;

    // Moving the original key and original entry to _map no copy
    auto &entry_ref = *pooled_entry;
    pq_.push(HeapNode{.key = std::string_view(entry_ref.key),
                      .expires_at = entry_ref.expires_at,
                      .ver = entry_ref.ver});

    auto [it, inserted] =
        _map.try_emplace(seastar::sstring(key_view), std::move(pooled_entry));
    if (!inserted) {
        /**
         * try_emplace inserts only when the key is absent, avoiding a separate
         * find+insert sequence.
         *
         * For updates (key already present), we replace the existing entry and
         * return the old one to the entry pool instead of letting it be
         * destroyed, preserving pooling semantics and avoiding allocator churn.
         */
        entry_pool_.release(std::exchange(it->second, std::move(pooled_entry)));
    }
    co_await check_memory_and_evict();

    co_return true;
}

seastar::future<std::optional<seastar::sstring>>
store::get(std::string_view key) {
    auto it = _map.find(key_t{key.data(), key.size()});
    if (it == _map.end()) {
        co_return std::nullopt;
    }
    const ttl::Entry &e = *(it->second);
    if (e.expires_at != 0 && now_s() >= e.expires_at) {
        sieve_policy_->on_erase(*(it->second));

        auto node = _map.extract(it);

        // node.mapped() is the unique_ptr! Pass it to the pool.
        entry_pool_.release(std::move(node.mapped()));
        co_return std::nullopt;
    }
    sieve_policy_->on_hit(*(it->second));
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
        kv_store_log.info("Memory pressure high {}, evicting keys.",
                          usage_fraction);
        if (!sieve_policy_.has_value()) {
            co_return;
        }

        const auto sieve_victims = co_await sieve_policy_->evict();
        const auto ttl_victims = co_await ttl_policy_.evict(now_s(), 200);

        auto process_victims =
            [&](std::vector<seastar::sstring> &victims) -> seastar::future<> {
            uint32_t count = 0;
            for (auto &key : victims) {
                auto node = _map.extract(key);
                if (node) {
                    // TODO: check if the entry is also in the other list
                }
            }
        } uint_32_t evicted_count = 0;
        for (const auto &victim_key : ttl_victims) {
            auto node = _map.extract(victim_key);
            if (node) {
                entry_pool_.release(std::move(node.mapped()));
            }
            if (evicted_count % 100 == 0) {
                co_await seastar::coroutine::maybe_yield();
            }
            evicted_count++;
        }
        evicted_count = 0;
        for (const auto &victim_key : sieve_victims) {
            auto node = _map.extract(victim_key);
            if (node) {
                entry_pool_.release(std::move(node.mapped()));
            }
            if (evicted_count % 100 == 0) {
                co_await seastar::coroutine::maybe_yield();
            }
            evicted_count++;
        }
    }
    co_return;
}

} // namespace shunyakv
