#include "kv_store.hh"
#include "allocator/EntryProvider.hh"
#include "hotpath_metrics.hh"
#include "proto_helpers.cc"
#include "ttl/entry.hh"
#include <cassert>
#include <chrono>
#include <coroutine>
#include <new>
#include <seastar/core/coroutine.hh>
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

seastar::future<> store::start(unsigned shard_id) {
    /**
     * Reserve a large amount here so that it doesn't reallocate while the db is
     * running.
     */
    _map.reserve(27000'00);
    provider_.emplace(sizeof(ttl::Entry), 64 * 1024, shard_id);
    co_return;
}

seastar::future<> store::stop() {
    if (provider_.has_value()) {
        for (auto &[k, entry] : _map) {
            (void)k;
            if (!entry) {
                continue;
            }
            entry->~Entry();
            provider_->deallocate(reinterpret_cast<std::byte *>(entry));
        }
    }
    _map.clear();
    absl::flat_hash_map<key_t, ttl::Entry *>().swap(_map);
    _map.rehash(0);
    provider_.reset();
    co_return;
}

seastar::future<bool> store::set(key_t key, seastar::sstring value) {
    if (!provider_.has_value()) {
        provider_.emplace(sizeof(ttl::Entry), 64 * 1024,
                          seastar::this_shard_id());
    }

    MemoryId mem;
    if (!provider_->try_fetch_entry(mem)) {
        kv_store_log.error(
            "provider allocation failed on shard {}: alloc_ops={}, "
            "alloc_fail_ops={}, total_pages_created={}, total_entries_created={}",
            seastar::this_shard_id(), provider_->alloc_ops(),
            provider_->alloc_fail_ops(), provider_->total_pages_created(),
            provider_->total_entries_created());
        co_return false;
    }
    assert(mem.size >= sizeof(ttl::Entry));

    ttl::Entry *e = new (mem.base) ttl::Entry();
    e->value = std::move(value);
    auto [it, inserted] = _map.try_emplace(std::move(key), e);
    if (!inserted) {
        ttl::Entry *old_entry = it->second;
        if (old_entry) {
            // Preserve existing TTL/metadata on plain SET overwrite.
            e->expires_at = old_entry->expires_at;
            e->ver = old_entry->ver;
            e->heat = old_entry->heat;
            e->last_access = old_entry->last_access;
        }
        it->second = e;
        if (old_entry) {
            old_entry->~Entry();
            provider_->deallocate(reinterpret_cast<std::byte *>(old_entry));
        }
    }
    co_return true;
}

seastar::future<bool> store::set_with_ttl(key_t key, seastar::sstring value,
                                          uint64_t ttl) {
    if (!provider_.has_value()) {
        provider_.emplace(sizeof(ttl::Entry), 64 * 1024,
                          seastar::this_shard_id());
    }

    MemoryId mem;
    if (!provider_->try_fetch_entry(mem)) {
        kv_store_log.error(
            "provider allocation failed on shard {}: alloc_ops={}, "
            "alloc_fail_ops={}, total_pages_created={}, total_entries_created={}",
            seastar::this_shard_id(), provider_->alloc_ops(),
            provider_->alloc_fail_ops(), provider_->total_pages_created(),
            provider_->total_entries_created());
        co_return false;
    }
    assert(mem.size >= sizeof(ttl::Entry));

    ttl::Entry *e = new (mem.base) ttl::Entry();
    e->value = std::move(value);
    e->expires_at = now_s() + ttl;
    auto [it, inserted] = _map.try_emplace(std::move(key), e);
    if (!inserted) {
        ttl::Entry *old_entry = it->second;
        it->second = e;
        if (old_entry) {
            old_entry->~Entry();
            provider_->deallocate(reinterpret_cast<std::byte *>(old_entry));
        }
    }
    co_return true;
}

seastar::future<std::optional<seastar::sstring>>
store::get(std::string_view key) {
    auto it = _map.find(key_t{key.data(), key.size()});
    if (it == _map.end()) {
        co_return std::nullopt;
    }
    ttl::Entry *e = it->second;
    if (!e) {
        co_return std::nullopt;
    }
    if (e->expires_at != 0 && now_s() >= e->expires_at) {
        if (provider_.has_value()) {
            e->~Entry();
            provider_->deallocate(reinterpret_cast<std::byte *>(e));
        }
        _map.erase(it);
        co_return std::nullopt;
    }
    co_return std::optional<seastar::sstring>(e->value);
}

} // namespace shunyakv
