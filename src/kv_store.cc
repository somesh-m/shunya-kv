#include "kv_store.hh"
#include "hotpath_metrics.hh"
#include "proto_helpers.cc"
#include "ttl/entry.hh"
#include <chrono>
#include <coroutine>
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

seastar::future<> store::start(unsigned) {
    /**
     * Reserve a large amount here so that it doesn't reallocate while the db is
     * running.
     */
    _map.reserve(27000'00);
    co_return;
}

seastar::future<> store::stop() {
    _map.clear();
    absl::flat_hash_map<key_t, ttl::Entry>().swap(_map);
    _map.rehash(0);
    co_return;
}

seastar::future<bool> store::set(key_t key, seastar::sstring value) {
    ttl::Entry &e = _map[std::move(key)];
    e.value = std::move(value);
    co_return true;
}

seastar::future<bool> store::set_with_ttl(key_t key, seastar::sstring value,
                                          uint64_t ttl) {
    ttl::Entry &e = _map[std::move(key)];
    e.value = std::move(value);
    e.expires_at = now_s() + ttl;
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
        co_return std::nullopt;
    }
    co_return std::optional<seastar::sstring>(e.value);
}

} // namespace shunyakv
