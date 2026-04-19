#include "kv_store.hh"
#include "hotpath_metrics.hh"
#include "proto_helpers.cc"
#include "ttl/entry.hh"
#include "ttl/heap_node.hh"
#include <chrono>
#include <coroutine>
#include <eviction/eviction_config.hh>
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

const char *to_string(eviction::EvictionType type) {
    return type == eviction::EvictionType::soft ? "soft" : "hard";
}
} // namespace

seastar::future<> store::start(unsigned, const db_config &cfg) {
    /**
     * Reserve a large amount here so that it doesn't reallocate while the db is
     * running.
     */

    set_usable_memory(cfg.pool.memory_reserve_percentage);
    kv_store_log.info("Memory reserve percent {}, usable memory {}",
                      cfg.pool.memory_reserve_percentage, usable_memory_);
    _map.reserve(27000'00);

    ev_cfg_ = cfg.ev_config;
    if (!sieve_policy_.has_value()) {
        sieve_policy_.emplace(ev_cfg_);
    }

    co_await entry_pool_.init(cfg, *sieve_policy_);
    entry_pool_.set_sequential_eviction_callback(
        [this](const std::vector<seastar::sstring> victimList)
            -> seastar::future<> {
            uint32_t count = 0;
            for (const auto &key : victimList) {
                auto node = _map.extract(key);
                if (!node.empty()) {
                    entry_pool_.release(std::move(node.mapped()));
                    stats_.record_eviction();
                }
                count++;
                if (count % 1000 == 0) {
                    co_await seastar::coroutine::maybe_yield();
                }
            }
            co_return;
        });
    co_return;
}

seastar::future<> store::stop() {
    _map.clear();
    absl::flat_hash_map<key_t, std::unique_ptr<ttl::Entry>>().swap(_map);
    _map.rehash(0);
    co_return;
}

seastar::future<bool> store::set(std::string_view key_view,
                                 seastar::sstring value) {
    if (entry_pool_.get_available_slots() == 0) {
        stats_.record_pool_fallback_alloc();
    }
    auto pooled_entry = co_await entry_pool_.acquire();
    if (!pooled_entry) {
        co_return false;
    }
    pooled_entry->key = seastar::sstring(key_view);
    pooled_entry->value = std::move(value);

    auto [it, inserted] =
        _map.try_emplace(seastar::sstring(key_view), std::move(pooled_entry));
    if (!inserted) {
        /**
         * The key already exists. 'it->second' is the unique_ptr to the OLD
         * entry. 'pooled_entry' is the unique_ptr to our NEW entry.
         */

        // 1. Inherit the metadata from the old entry
        // We keep the version, heat, and access times so the
        // eviction policies (Sieve/PQ) stay consistent.
        it->second->update_from(std::move(*pooled_entry), false);
        /**
         * Since the key was already present, promote it to sanctuary
         */
        entry_pool_.promote_to_sanctuary(*it->second.get());

        entry_pool_.release(std::move(pooled_entry));
    }
    // sieve_policy_->on_insert(*it->second.get());
    co_await check_memory_and_evict();
    co_return true;
}

/**
 * TODO: Merge set_with_ttl and set into one function to avoid code redudency
 */
seastar::future<bool> store::set_with_ttl(std::string_view key_view,
                                          seastar::sstring value,
                                          uint64_t ttl) {
    const auto op_start = std::chrono::steady_clock::now();
    if (entry_pool_.get_available_slots() == 0) {
        stats_.record_pool_fallback_alloc();
    }

    const auto acquire_start = std::chrono::steady_clock::now();
    auto pooled_entry = co_await entry_pool_.acquire();
    const auto acquire_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - acquire_start)
            .count();
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

    const auto map_start = std::chrono::steady_clock::now();
    auto [it, inserted] =
        _map.try_emplace(seastar::sstring(key_view), std::move(pooled_entry));
    const auto map_us = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - map_start)
                            .count();
    if (!inserted) {
        /**
         * try_emplace inserts only when the key is absent, avoiding a separate
         * find+insert sequence.
         *
         * For updates (key already present), we replace the existing entry and
         * return the old one to the entry pool instead of letting it be
         * destroyed, preserving pooling semantics and avoiding allocator churn.
         */
        it->second->update_from(std::move(*pooled_entry), true);

        entry_pool_.promote_to_sanctuary(*it->second.get());

        entry_pool_.release(std::move(pooled_entry));
    }

    co_await check_memory_and_evict();

    co_return true;
}

seastar::future<std::optional<seastar::sstring>>
store::get(std::string_view key) {
    auto it = _map.find(seastar::sstring(key));
    if (it == _map.end()) {
        co_return std::nullopt;
    }
    auto &entry_ptr = it->second;
    if (entry_ptr->expires_at != 0 && now_s() >= entry_ptr->expires_at) {
        sieve_policy_->on_erase(*entry_ptr);

        auto node = _map.extract(it);
        // node.mapped() is the unique_ptr! Pass it to the pool.
        entry_pool_.release(std::move(node.mapped()));
        co_return std::nullopt;
    }
    entry_pool_.promote_to_sanctuary(*entry_ptr);
    sieve_policy_->on_hit(*entry_ptr);
    co_return std::optional<seastar::sstring>(entry_ptr->value);
}

void store::set_usable_memory(double reserve_percentage) {
    auto stats = seastar::memory::stats();
    // Keeping 15 percent reserved for seastar overhead
    usable_memory_ = (1 - reserve_percentage) * stats.total_memory();
}

shard_stats_snapshot store::snapshot_stats() const noexcept {
    return stats_.snapshot(entry_pool_.get_available_slots(),
                           entry_pool_.get_total_slots(), _map.size(),
                           entry_pool_.get_total_prob_slots(),
                           entry_pool_.get_used_prob_slots(),
                           entry_pool_.get_prob_eviction_count());
}

seastar::future<> store::check_memory_and_evict() {
    const std::size_t total_slots = entry_pool_.get_total_slots();
    if (total_slots == 0) {
        co_return;
    }

    // 1. Get memory statistics for the current shard
    auto stats = seastar::memory::stats();

    // total_memory() is the maximum allowed for this shard
    // allocated_memory() is what is currently used
    size_t currently_used = stats.allocated_memory();
    // size_t limit = stats.total_memory();
    size_t limit = usable_memory_;

    // 2. Calculate percentage
    double pool_usage_fraction =
        static_cast<double>(entry_pool_.get_used_slots()) / total_slots;

    // 3. Trigger eviction if over threshold (e.g., 80%)
    const double soft_trigger = ev_cfg_.soft_.trigger;

    // kv_store_log.info("Pool usage fraction {} soft trigger {}",
    //                   pool_usage_fraction, soft_trigger);
    eviction::EvictConfig config;
    eviction::EvictionType type;
    // soft eviction applies on pool and hard trigger on total memory
    if (pool_usage_fraction >= soft_trigger) {
        type = eviction::EvictionType::soft;
        sieve_policy_->set_eviction_type(type);
        HOTPATHLOGS(
            kv_store_log.info("Starting {} eviction\n Usage fraction {}",
                              to_string(type), pool_usage_fraction));
        // TODO: Remove ttl related code after establishing correctness
        // co_await evict_ttl_keys(now_s(), 300);
        if (!sieve_policy_.has_value())
            co_return;
        const auto sieve_victims = co_await sieve_policy_->evict(now_s());
        // kv_store_log.info("Victim Keys {}", sieve_victims.size());
        uint32_t count = 0;

        for (const auto &key : sieve_victims) {
            auto node = _map.extract(key);
            if (!node.empty()) {
                entry_pool_.release(std::move(node.mapped()));
                stats_.record_eviction();
            }
            count++;
            if (count % 1000 == 0) {
                co_await seastar::coroutine::maybe_yield();
            }
        }
    }
    co_return;
}

/**
 * TTL eviction is an integral part of the key store, not a pluggable logic, so
 * we don't need to move it into a separate class
 */
future<> store::evict_ttl_keys(uint64_t now, std::size_t budget) {
    std::size_t removed = 0;

    while (budget > 0 && !pq_.empty()) {
        const ttl::HeapNode top = pq_.top();
        if (!is_expired(now, top.expires_at))
            break;

        pq_.pop();
        budget--; // Decrement budget for each attempted eviction

        auto mEnt = _map.extract(seastar::sstring(top.key));

        if (!mEnt.empty()) {
            // Check if the entry in the map is actually the one the PQ refers
            // to
            if (mEnt.mapped()->ver == top.ver) {
                // 1. Unlink from Sieve
                sieve_policy_->on_erase(*mEnt.mapped());
                // 2. Recycle to Pool
                entry_pool_.release(std::move(mEnt.mapped()));
            } else {
                // Version mismatch: the key was updated/replaced.
                // Re-insert the "New" entry back into the map.
                _map.insert(std::move(mEnt));
            }
        }

        if (++removed % 100 == 0) {
            co_await seastar::coroutine::maybe_yield();
        }
    }
    co_return;
}
} // namespace shunyakv
