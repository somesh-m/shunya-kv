#pragma once
#include "dbconfig.hh"
#include "kv_store.hh"
#include "router_metrics.hh"
#include "vector/global_centroid_routing.hh"
#include "vector/vector_index_manager.hh"

#include "hotpath_metrics.hh"
#include "pool/shard_memory_manager.hh"
#include "shard_stats.hh"
#include "vector/vector_routing.hh"
#include <cstdint>
#include <functional>
#include <optional>
#include <seastar/core/abort_source.hh>
#include <seastar/core/future.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/sstring.hh>
#include <seastar/core/temporary_buffer.hh>
#include <seastar/net/api.hh>
#include <string_view>

using namespace seastar;
namespace shunyakv {

struct VectorStoreInfoSnapshot {
    std::vector<seastar::sstring> local_indexes;
    std::size_t local_entry_count = 0;
    bool index_enabled = false;
    bool index_building = false;
};

class service : public seastar::peering_sharded_service<service> {
  public:
    future<> start(const db_config &cfg);
    future<> stop();
    // Might be using sstring for value in local_set is not the best choice.
    future<bool> local_set(std::string_view, sstring);
    future<bool> local_set(std::string_view, sstring, uint64_t);
    future<std::optional<sstring>> local_get(std::string_view);
    future<std::optional<sstring>> local_vget(std::string_view,
                                              std::string_view);
    future<std::optional<sstring>> vget(std::string_view, std::string_view);
    future<bool> vdelete(std::string_view index, std::string_view key);
    future<bool> local_vset(std::string_view index, std::string_view key,
                            std::vector<float> embedding, std::string value,
                            centroid_id centroid);
    future<bool> local_vdelete(std::string_view index, std::string_view key);
    future<std::vector<VectorSearchResult>>
    local_vsearch(std::string_view index, std::vector<float> query_embedding,
                  uint32_t top_k, std::vector<centroid_id> centroids);
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
    VectorStoreInfoSnapshot snapshot_vector_store_info() const;

  private:
    future<> ensure_started();
    future<> publish_vector_owner(key_t key,
                                  std::optional<VectorPoint> owner);
    // Shard Memory manager is created per shard. It manages the memory usage at
    // each shard level
    std::optional<db_config> config_;
    std::optional<pool::ShardMemoryManager> memory_manager_;
    std::optional<EntryPool> vector_entry_pool_;
    std::optional<VectorIndexManager> vector_index_manager_;
    GlobalCentroidRouting global_centroid_routing_;

    // Instantiate any other store here in the future.
    store _store;
    bool _started{false};
    request_counters _req_counters;
    request_latency_counters _latency_counters;
};

inline service &local_service() {
    static thread_local service svc;
    return svc;
}

} // namespace shunyakv
