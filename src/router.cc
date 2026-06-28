// router.cc
#include "router.hh"

#include <algorithm>
#include <iterator>
#include <utility>

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
        pending.push_back(coordinator.container().invoke_on(
            target_shard,
            [index_copy = owned_index, embedding_copy = embedding,
             centroid_ids = std::move(centroid_ids), top_k](
                service &local_service) mutable -> future<LocalResult> {
                LocalResult shard_candidates;

                for (const centroid_id id : centroid_ids) {
                    LocalResult centroid_results =
                        co_await local_service.vector_store_.vsearch(
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
    _started = true;
    co_return;
}

future<> service::stop() {
    if (!_started) {
        co_return;
    }
    co_await vector_store_.stop();
    co_await _store.stop();
    _started = false;
    co_return;
}

future<bool> service::local_vset(std::string_view index, std::string_view key,
                                 std::vector<float> embedding,
                                 std::string value) {
    co_await ensure_started();

    auto owner_shards = co_await find_vector_owner_shard(embedding, index);
    (void)owner_shards;

    centroid_id target_centroid = 0;
    co_return co_await vector_store_.vset(index, key, std::move(embedding),
                                          std::move(value), target_centroid);
}

future<std::vector<VectorSearchResult>>
service::vsearch(std::string_view index, std::vector<float> query_embedding) {
    co_await ensure_started();

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

future<std::vector<usize_t>>
service::find_vector_owner_shard(std::span<const float> embedding,
                                 std::string_view index) {
    (void)embedding;
    (void)index;
    co_return std::vector<usize_t>{};
}

future<scatter_result>
service::find_global_top_centroids(std::span<const float> query_embedding,
                                   std::string_view index) {
    const std::size_t nprobe = vdb_orch_.nprobe();

    if (query_embedding.empty() || nprobe == 0) {
        co_return scatter_result{};
    }

    std::vector<float> query{
        query_embedding.begin(),
        query_embedding.end(),
    };

    std::vector<CentroidScore> global_winners = co_await container().map_reduce0(
        [query = std::move(query), index](service &local_service) {
            std::vector<float> local_query{
                query.begin(),
                query.end(),
            };
            return local_service.vdb_orch_.find_top_centroids(local_query,
                                                              index);
        },
        std::vector<CentroidScore>{},
        [](std::vector<CentroidScore> global,
           std::vector<CentroidScore> local) mutable {
            global.insert(global.end(), std::make_move_iterator(local.begin()),
                          std::make_move_iterator(local.end()));
            return global;
        });

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
