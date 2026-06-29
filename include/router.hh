#pragma once
#include "dbconfig.hh"
#include "kv_store.hh"
#include "router_metrics.hh"
#include "vector/vector_store.hh"

#include "hotpath_metrics.hh"
#include "pool/shard_memory_manager.hh"
#include "shard_stats.hh"
#include "vector/vector_routing.hh"
#include <cstdint>
#include <functional>
#include <optional>
#include <seastar/core/future.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/sstring.hh>
#include <seastar/core/temporary_buffer.hh>
#include <seastar/net/api.hh>
#include <string_view>

using namespace seastar;
namespace shunyakv {

class service : public seastar::peering_sharded_service<service> {
  public:
    future<> start(const db_config &cfg);
    future<> stop();
    // Might be using sstring for value in local_set is not the best choice.
    future<bool> local_set(std::string_view, sstring);
    future<bool> local_set(std::string_view, sstring, uint64_t);
    future<std::optional<sstring>> local_get(std::string_view);
    future<bool> local_vset(std::string_view index, std::string_view key,
                            std::vector<float> embedding, std::string value,
                            centroid_id centroid);
    future<std::vector<VectorSearchResult>>
    local_vsearch(std::string_view index, std::vector<float> query_embedding,
                  uint32_t top_k, centroid_id centroid);
    future<std::vector<VectorSearchResult>>
    local_vsearch_brute(std::string_view index,
                        std::vector<float> query_embedding, uint32_t top_k);
    future<bool> vset(std::string_view index, std::string_view key,
                      std::vector<float> embedding, std::string value);
    future<std::vector<VectorSearchResult>>
    vsearch(std::string_view index, std::vector<float> query_embedding);
    void record_get(bool) noexcept;
    void record_set(bool) noexcept;
    void record_get_latency(uint64_t) noexcept;
    void record_set_latency(uint64_t) noexcept;
    void record_cache_miss() noexcept;
    void record_cache_miss_count(std::size_t n = 1) noexcept;
    request_counters snapshot_request_counters() const noexcept;
    request_latency_counters snapshot_request_latency_counters() const noexcept;
    shard_stats_snapshot snapshot_shard_stats() const noexcept;

    // Vector DB Related functions
    future<scatter_result>
    find_global_top_centroids(std::span<const float> query_embedding,
                              std::string_view index,
                              std::optional<uint32_t> result_count =
                                  std::nullopt);

    future<VectorPoint>
    find_vector_owner_shard(std::span<const float> embedding,
                            std::string_view index);

    future<shard_id> find_storage_shard(std::span<const float>);

  private:
    future<> ensure_started();
    // Shard Memory manager is created per shard. It manages the memory usage at
    // each shard level
    std::optional<pool::ShardMemoryManager> memory_manager_;

    // Instantiate any other store here in the future.
    store _store;
    VectorStore vector_store_;
    // Instantiate the vdb orchestrator as well here
    ::vdb::VDBOrchestrator vdb_orch_;
    bool _started{false};
    request_counters _req_counters;
    request_latency_counters _latency_counters;
};

inline service &local_service() {
    static thread_local service svc;
    return svc;
}

} // namespace shunyakv
