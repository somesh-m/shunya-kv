// router.cc
#include "router.hh"

#include <algorithm>
#include <utility>

#include <seastar/core/future-util.hh>
#include <seastar/core/sleep.hh>
#include <seastar/util/log.hh>

using namespace seastar;
namespace shunyakv {

namespace {

constexpr uint32_t kDefaultVsearchTopK = 5;
constexpr std::size_t kDefaultVsearchNprobe = 8;
static seastar::logger router_logger{"router"};

} // namespace

future<> service::ensure_started() {
    if (_started) {
        co_return;
    }
    db_config cfg;
    co_await start(cfg);
}

future<> service::start(const db_config &cfg) {
    if (_started) {
        co_return;
    }
    config_.emplace(cfg);
    memory_manager_.emplace(*config_);
    vector_entry_pool_.emplace(*memory_manager_, *config_, 1000, 20);
    co_await vector_entry_pool_->init();
    vector_index_manager_.emplace(*vector_entry_pool_);

    if (!global_centroid_routing_.load_from_file(
            "/home/sohmesh-mohan/kv/global_routing.tbl")) {
        router_logger.warn("Unable to load centroid table '{}' on shard {}",
                           config_->centroid_table_path, this_shard_id());
    } else {
        router_logger.info("Vector table config parsed: path='{}', "
                           "centroids={}, dimension={}, shard={}",
                           "/home/sohmesh-mohan/kv/global_routing.tbl",
                           global_centroid_routing_.num_centroids(),
                           global_centroid_routing_.dimensions(),
                           this_shard_id());
    }

    co_await _store.start(this_shard_id(), cfg, *memory_manager_);
    _started = true;
    co_return;
}

future<> service::stop() {
    if (!_started) {
        co_return;
    }
    co_await _store.stop();
    vector_index_manager_.reset();
    vector_entry_pool_.reset();
    memory_manager_.reset();
    config_.reset();
    _started = false;
    co_return;
}

future<> service::publish_vector_owner(key_t key,
                                       std::optional<VectorPoint> owner) {
    std::vector<future<>> pending;
    pending.reserve(seastar::smp::count);
    for (unsigned shard = 0; shard < seastar::smp::count; ++shard) {
        pending.push_back(seastar::smp::submit_to(shard, [key_copy = key,
                                                          owner]() mutable {
            auto &routing = shunyakv::local_service().global_centroid_routing_;
            if (owner.has_value()) {
                routing.remember_owner(std::move(key_copy), *owner);
            } else {
                routing.forget_owner(key_copy);
            }
            return seastar::make_ready_future<>();
        }));
    }
    co_await seastar::when_all_succeed(pending.begin(), pending.end());
}

future<std::optional<sstring>> service::vget(std::string_view key,
                                             std::string_view index) {
    co_await ensure_started();
    (void)index;
    const key_t owned_key{key};
    const auto owner = global_centroid_routing_.owner_for(owned_key);
    if (!owner || owner->target_shard_id >= seastar::smp::count) {
        co_return std::nullopt;
    }
    if (owner->target_shard_id == this_shard_id()) {
        co_return co_await local_vget(key, index);
    }
    co_return co_await seastar::smp::submit_to(
        owner->target_shard_id, [key = std::string(key)]() mutable {
            return shunyakv::local_service().local_vget(key, {});
        });
}

future<bool> service::vdelete(std::string_view index, std::string_view key) {
    co_await ensure_started();

    (void)index;
    const key_t owned_key{key};
    const auto owner = global_centroid_routing_.owner_for(owned_key);
    if (!owner || owner->target_shard_id >= seastar::smp::count) {
        co_return false;
    }

    bool removed = false;
    if (owner->target_shard_id == seastar::this_shard_id()) {
        removed = co_await local_vdelete({}, key);
    } else {
        removed = co_await seastar::smp::submit_to(
            owner->target_shard_id, [key = std::string(key)]() mutable {
                return shunyakv::local_service().local_vdelete({}, key);
            });
    }
    co_await publish_vector_owner(std::move(owned_key), std::nullopt);
    co_return removed;
}

future<bool> service::vset(std::string_view index, std::string_view key,
                           std::vector<float> embedding, std::string value) {
    co_await ensure_started();
    (void)index;
    const auto owner_points =
        global_centroid_routing_.route_embedding(embedding, 1);
    if (owner_points.empty() ||
        owner_points.front().target_shard_id >= seastar::smp::count) {
        co_return false;
    }
    const VectorPoint owner_point = owner_points.front();

    const key_t owned_key{key};
    const auto previous_owner = global_centroid_routing_.owner_for(owned_key);
    if (previous_owner &&
        previous_owner->target_shard_id < seastar::smp::count) {
        if (previous_owner->target_shard_id == this_shard_id()) {
            co_await local_vdelete({}, key);
        } else {
            co_await seastar::smp::submit_to(
                previous_owner->target_shard_id,
                [key = std::string(key)]() mutable {
                    return shunyakv::local_service().local_vdelete({}, key);
                });
        }
    }

    bool stored = false;
    if (owner_point.target_shard_id == seastar::this_shard_id()) {
        stored = co_await local_vset({}, key, std::move(embedding),
                                     std::move(value), owner_point.id);
    } else {
        stored = co_await seastar::smp::submit_to(
            owner_point.target_shard_id,
            [key = std::string(key), embedding = std::move(embedding),
             value = std::move(value), centroid = owner_point.id]() mutable {
                return shunyakv::local_service().local_vset(
                    {}, key, std::move(embedding), std::move(value), centroid);
            });
    }
    if (stored) {
        co_await publish_vector_owner(owned_key, owner_point);
    } else if (previous_owner) {
        co_await publish_vector_owner(owned_key, std::nullopt);
    }
    co_return stored;
}

future<bool> service::local_vset(std::string_view index, std::string_view key,
                                 std::vector<float> embedding,
                                 std::string value, centroid_id centroid) {
    co_await ensure_started();
    (void)index;
    co_return co_await vector_index_manager_->insert(
        key_t{key}, std::move(embedding), std::move(value), centroid);
}

future<bool> service::local_vdelete(std::string_view index,
                                    std::string_view key) {
    co_await ensure_started();
    (void)index;
    co_return co_await vector_index_manager_->erase(key_t{key});
}

future<std::vector<VectorSearchResult>>
service::local_vsearch(std::string_view index,
                       std::vector<float> query_embedding, uint32_t top_k,
                       std::vector<centroid_id> centroids) {
    co_await ensure_started();
    (void)index;
    VectorResultQueue winners;
    for (const centroid_id centroid : centroids) {
        auto centroid_results = co_await vector_index_manager_->search(
            query_embedding, centroid, top_k);
        for (auto &result : centroid_results) {
            winners.push(std::move(result));
            if (winners.size() > top_k) {
                winners.pop();
            }
        }
    }

    std::vector<VectorSearchResult> results;
    results.reserve(winners.size());
    while (!winners.empty()) {
        results.push_back(winners.top());
        winners.pop();
    }
    std::reverse(results.begin(), results.end());
    co_return results;
}

future<std::vector<VectorSearchResult>>
service::local_vsearch_brute(std::string_view index,
                             std::vector<float> query_embedding,
                             uint32_t top_k) {
    co_await ensure_started();
    (void)index;
    const auto owners =
        global_centroid_routing_.route_embedding(query_embedding, 1);
    if (owners.empty()) {
        co_return std::vector<VectorSearchResult>{};
    }
    co_return co_await local_vsearch({}, std::move(query_embedding), top_k,
                                     {owners.front().id});
}

future<std::vector<VectorSearchResult>>
service::vsearch(std::string_view index, std::vector<float> query_embedding) {
    co_await ensure_started();
    (void)index;
    const auto owners =
        global_centroid_routing_.route_embedding(query_embedding, 2);
    if (owners.empty()) {
        co_return std::vector<VectorSearchResult>{};
    }

    std::vector<std::vector<centroid_id>> centroids_by_shard(
        seastar::smp::count);
    for (const VectorPoint &owner : owners) {
        if (owner.target_shard_id < seastar::smp::count) {
            centroids_by_shard[owner.target_shard_id].push_back(owner.id);
        }
    }

    std::vector<future<std::vector<VectorSearchResult>>> pending;
    pending.reserve(seastar::smp::count);
    for (unsigned shard = 0; shard < seastar::smp::count; ++shard) {
        if (centroids_by_shard[shard].empty()) {
            continue;
        }
        pending.push_back(seastar::smp::submit_to(
            shard,
            [embedding = query_embedding,
             centroids = std::move(centroids_by_shard[shard])]() mutable {
                return shunyakv::local_service().local_vsearch(
                    {}, std::move(embedding), kDefaultVsearchTopK,
                    std::move(centroids));
            }));
    }

    auto per_shard =
        co_await seastar::when_all_succeed(pending.begin(), pending.end());
    VectorResultQueue winners;
    for (auto &shard_results : per_shard) {
        for (auto &result : shard_results) {
            winners.push(std::move(result));
            if (winners.size() > kDefaultVsearchTopK) {
                winners.pop();
            }
        }
    }

    std::vector<VectorSearchResult> results;
    results.reserve(winners.size());
    while (!winners.empty()) {
        results.push_back(winners.top());
        winners.pop();
    }
    std::reverse(results.begin(), results.end());
    co_return results;
}

future<bool> service::local_set(std::string_view key, sstring value) {
    co_await ensure_started();
    key_t k{key.data(), key.size()};
    co_return co_await _store.set(std::move(k), std::move(value));
}

future<bool> service::local_set(std::string_view key, sstring value,
                                uint64_t ttl) {
    co_await ensure_started();
    key_t k{key.data(), key.size()};
    co_return co_await _store.set_with_ttl(std::move(k), std::move(value), ttl);
}

future<std::optional<sstring>> service::local_get(std::string_view key) {
    co_await ensure_started();
    co_return co_await _store.get(key);
}

future<std::optional<sstring>> service::local_vget(std::string_view key,
                                                   std::string_view index) {
    co_await ensure_started();
    (void)index;
    co_return vector_index_manager_->get(key_t{key});
}

void service::record_set(bool forwarded) noexcept {
    ++_req_counters.set_total;
    if (forwarded) {
        ++_req_counters.set_forwarded;
    }
}

void service::record_get(bool forwarded) noexcept {
    ++_req_counters.get_total;
    if (forwarded) {
        ++_req_counters.get_forwarded;
    }
}

void service::record_get_latency(uint64_t latency_us) noexcept {
    _latency_counters.total.add_us(latency_us);
}

void service::record_set_latency(uint64_t latency_us) noexcept {
    _latency_counters.total.add_us(latency_us);
}

void service::record_cache_miss() noexcept { ++_req_counters.cache_miss; }

void service::record_cache_miss_count(std::size_t n) noexcept {
    _store.record_cache_miss_count(n);
}

request_counters service::snapshot_request_counters() const noexcept {
    return _req_counters;
}

request_latency_counters
service::snapshot_request_latency_counters() const noexcept {
    return _latency_counters;
}

shard_stats_snapshot service::snapshot_shard_stats() const noexcept {
    return _store.snapshot_stats();
}

VectorStoreInfoSnapshot service::snapshot_vector_store_info() const {
    VectorStoreInfoSnapshot snapshot;
    if (vector_index_manager_) {
        snapshot.local_entry_count =
            vector_index_manager_->get_storage().total_count();
    }
    snapshot.index_enabled = global_centroid_routing_.loaded();
    return snapshot;
}

} // namespace shunyakv
