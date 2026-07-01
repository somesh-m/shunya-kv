// router.cc
#include "router.hh"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <utility>

#include <seastar/core/future-util.hh>
#include <seastar/core/sleep.hh>

using namespace seastar;
namespace shunyakv {

namespace {

constexpr uint32_t kDefaultVsearchTopK = 5;

future<std::vector<VectorSearchResult>>
fanout_search(service &coordinator, std::string_view index,
              std::vector<float> embedding, scatter_result top_centroids,
              uint32_t top_k) {
    using LocalResult = std::vector<VectorSearchResult>;

    if (top_k == 0 || top_centroids.empty()) {
        co_return LocalResult{};
    }

    std::string owned_index{index};

    std::vector<future<LocalResult>> pending;
    pending.reserve(top_centroids.size());

    for (auto &[target_shard, centroid_ids] : top_centroids) {
        pending.push_back(seastar::smp::submit_to(
            target_shard,
            [index_copy = owned_index, embedding_copy = embedding,
             centroid_ids = std::move(centroid_ids),
             top_k]() mutable -> future<LocalResult> {
                LocalResult shard_candidates;

                for (const centroid_id id : centroid_ids) {
                    LocalResult centroid_results =
                        co_await shunyakv::local_service().local_vsearch(
                            index_copy, embedding_copy, top_k, id);

                    shard_candidates.insert(
                        shard_candidates.end(),
                        std::make_move_iterator(centroid_results.begin()),
                        std::make_move_iterator(centroid_results.end()));
                }

                co_return shard_candidates;
            }));
    }

    auto shard_results =
        co_await when_all_succeed(pending.begin(), pending.end());

    VectorResultQueue global_winners;
    for (auto &shard_candidates : shard_results) {
        for (auto &candidate : shard_candidates) {
            global_winners.push(std::move(candidate));
            if (global_winners.size() > top_k) {
                global_winners.pop();
            }
        }
    }

    LocalResult results;
    results.reserve(global_winners.size());
    while (!global_winners.empty()) {
        results.push_back(global_winners.top());
        global_winners.pop();
    }

    std::reverse(results.begin(), results.end());
    co_return results;
}

} // namespace

future<std::size_t> service::fetch_vector_entry_count() {
    std::vector<future<std::size_t>> pending;
    pending.reserve(seastar::smp::count);

    for (unsigned shard = 0; shard < seastar::smp::count; ++shard) {
        pending.push_back(seastar::smp::submit_to(shard, [] {
            return shunyakv::local_service().vector_store_.local_entry_count();
        }));
    }

    auto shard_totals =
        co_await seastar::when_all_succeed(pending.begin(), pending.end());

    std::size_t global_total = 0;
    for (std::size_t local_total : shard_totals) {
        global_total += local_total;
    }

    co_return global_total;
}

future<> service::publish_routing_snapshots(
    std::vector<LocalCentroidSnapshot> snapshots) {
    if (snapshots.empty()) {
        co_return;
    }

    std::vector<future<>> pending;
    pending.reserve(seastar::smp::count);

    for (unsigned shard = 0; shard < seastar::smp::count; ++shard) {
        pending.push_back(seastar::smp::submit_to(
            shard, [snapshots = snapshots]() mutable {
                auto &local_service = shunyakv::local_service();

                for (const auto &snapshot : snapshots) {
                    local_service.vdb_orch_.replace_index_centroids(snapshot);
                }

                return seastar::make_ready_future<>();
            }));
    }

    co_await seastar::when_all_succeed(pending.begin(), pending.end());
}

future<> service::bg_count_checker() {
    try {
        while (true) {
            co_await seastar::sleep_abortable(std::chrono::seconds(30),
                                              _index_build_as);

            const auto count = co_await fetch_vector_entry_count();
            if (count < 10 * seastar::smp::count) {
                continue;
            }

            std::vector<future<std::vector<LocalCentroidSnapshot>>> pending;
            pending.reserve(seastar::smp::count);

            for (unsigned shard = 0; shard < seastar::smp::count; ++shard) {
                pending.push_back(seastar::smp::submit_to(shard, [] {
                    return shunyakv::local_service().vector_store_
                        .build_local_index();
                }));
            }

            auto local_snapshots =
                co_await seastar::when_all_succeed(pending.begin(),
                                                   pending.end());

            std::vector<LocalCentroidSnapshot> snapshots_to_publish;
            for (auto &snapshot_group : local_snapshots) {
                snapshots_to_publish.insert(
                    snapshots_to_publish.end(),
                    std::make_move_iterator(snapshot_group.begin()),
                    std::make_move_iterator(snapshot_group.end()));
            }

            co_await publish_routing_snapshots(std::move(snapshots_to_publish));
            co_return;
        }
    } catch (const seastar::abort_requested_exception &) {
        co_return;
    }
}

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
    memory_manager_.emplace(cfg);

    co_await vector_store_.start(this_shard_id(), cfg, *memory_manager_);
    co_await _store.start(this_shard_id(), cfg, *memory_manager_);
    _index_build_task.emplace(bg_count_checker());
    _started = true;
    co_return;
}

future<> service::stop() {
    if (!_started) {
        co_return;
    }
    _index_build_as.request_abort();
    if (_index_build_task && _index_build_task->available()) {
        co_await std::move(*_index_build_task);
        _index_build_task.reset();
    } else if (_index_build_task) {
        co_await std::move(*_index_build_task);
        _index_build_task.reset();
    }
    co_await vector_store_.stop();
    co_await _store.stop();
    _started = false;
    co_return;
}

future<bool> service::vset(std::string_view index, std::string_view key,
                           std::vector<float> embedding, std::string value) {
    co_await ensure_started();
    if (!vector_store_.is_index_enabled()) {
        // Index is not built yet, directly store this
        co_return co_await vector_store_.vset_brute(
            index, key, std::move(embedding), std::move(value));
    }
    const VectorPoint owner_point =
        co_await find_vector_owner_shard(embedding, index);

    if (owner_point.target_shard_id == seastar::this_shard_id()) {
        co_return co_await local_vset(index, key, std::move(embedding),
                                      std::move(value), owner_point.id);
    }

    std::string owned_index{index};
    std::string owned_key{key};
    const centroid_id owner_centroid = owner_point.id;

    co_return co_await seastar::smp::submit_to(
        owner_point.target_shard_id,
        [index = std::move(owned_index), key = std::move(owned_key),
         embedding = std::move(embedding), value = std::move(value),
         owner_centroid]() mutable {
            return shunyakv::local_service().local_vset(
                index, key, std::move(embedding), std::move(value),
                owner_centroid);
        });
}

future<bool> service::local_vset(std::string_view index, std::string_view key,
                                 std::vector<float> embedding,
                                 std::string value, centroid_id centroid) {
    co_await ensure_started();
    co_return co_await vector_store_.vset(index, key, std::move(embedding),
                                          std::move(value), centroid);
}

future<std::vector<VectorSearchResult>>
service::local_vsearch(std::string_view index,
                       std::vector<float> query_embedding, uint32_t top_k,
                       centroid_id centroid) {
    co_await ensure_started();
    co_return co_await vector_store_.vsearch(index, std::move(query_embedding),
                                             top_k, centroid);
}

future<std::vector<VectorSearchResult>>
service::local_vsearch_brute(std::string_view index,
                             std::vector<float> query_embedding,
                             uint32_t top_k) {
    co_await ensure_started();
    co_return co_await vector_store_.vsearch_brute(
        index, std::move(query_embedding), top_k);
}

future<std::vector<VectorSearchResult>>
service::vsearch(std::string_view index, std::vector<float> query_embedding) {
    co_await ensure_started();
    if (!vector_store_.is_index_enabled()) {
        co_return co_await local_vsearch_brute(
            index, std::move(query_embedding), kDefaultVsearchTopK);
    }
    const scatter_result top_centroids =
        co_await find_global_top_centroids(query_embedding, index);
    if (top_centroids.empty()) {
        co_return std::vector<VectorSearchResult>{};
    }

    co_return co_await fanout_search(*this, index, std::move(query_embedding),
                                     top_centroids, kDefaultVsearchTopK);
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

future<VectorPoint>
service::find_vector_owner_shard(std::span<const float> embedding,
                                 std::string_view index) {
    /**
     * To find the top nearest centroid, call the find_global_top_centroids with
     * result count set as 1. find_global_top_centroids function scatters
     * request to all the shards to find the top_k nearest centroids to an
     * embedding.
     */
    scatter_result nearest_centroid =
        co_await find_global_top_centroids(embedding, index, 1);

    /**
     * Send the write request to nearest_centroid->first shard and
     * nearest_centroid->second centroid
     */
    VectorPoint result;
    for (const auto &[target_shard, centroid_ids] : nearest_centroid) {
        if (centroid_ids.empty()) {
            continue;
        }
        result = VectorPoint{
            .id = *centroid_ids.begin(),
            .target_shard_id = target_shard,
        };
        break;
    }

    co_return result;
}

future<scatter_result>
service::find_global_top_centroids(std::span<const float> query_embedding,
                                   std::string_view index,
                                   std::optional<uint32_t> result_count) {
    const uint32_t nprobe = result_count.value_or(vdb_orch_.nprobe());

    if (query_embedding.empty() || nprobe == 0) {
        co_return scatter_result{};
    }

    std::vector<float> query{
        query_embedding.begin(),
        query_embedding.end(),
    };

    std::vector<future<std::vector<CentroidScore>>> pending;
    pending.reserve(seastar::smp::count);

    for (unsigned shard = 0; shard < seastar::smp::count; ++shard) {
        pending.push_back(seastar::smp::submit_to(
            shard, [query, owned_index = std::string(index)]() mutable {
                return shunyakv::local_service().vdb_orch_.find_top_centroids(
                    query, owned_index);
            }));
    }

    auto local_winners =
        co_await seastar::when_all_succeed(pending.begin(), pending.end());

    std::vector<CentroidScore> global_winners;
    for (auto &winner_group : local_winners) {
        global_winners.insert(global_winners.end(),
                              std::make_move_iterator(winner_group.begin()),
                              std::make_move_iterator(winner_group.end()));
    }

    std::priority_queue<CentroidScore, std::vector<CentroidScore>,
                        std::greater<CentroidScore>>
        combined_queue;
    for (auto &winner : global_winners) {
        combined_queue.push(std::move(winner));
        if (combined_queue.size() > nprobe) {
            combined_queue.pop();
        }
    }

    scatter_result result;
    while (!combined_queue.empty()) {
        CentroidScore score = combined_queue.top();
        combined_queue.pop();
        result[score.target_shard_id].insert(score.id);
    }

    co_return result;
}

} // namespace shunyakv
