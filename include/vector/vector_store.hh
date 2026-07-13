#pragma once
#include "dbconfig.hh"
#include "eviction/sieve_policy.hh"
#include "pool/shard_memory_manager.hh"
#include "shard_stats.hh"
#include "vector_entry.hh"
#include "vector_index.hh"
#include "vector_types.hh"
#include <absl/container/flat_hash_map.h>
#include <memory>
#include <optional>
#include <queue>
#include <seastar/core/future.hh>
#include <string>
#include <string_view>
#include <vector>

/**
 * Each shard is supposed to have it's own copy of vector store
 */

using namespace seastar;
namespace shunyakv {
struct VectorStoreInfoSnapshot {
    std::vector<seastar::sstring> local_indexes;
    std::size_t local_entry_count = 0;
    bool index_enabled = false;
    bool index_building = false;
};

class VectorStore {

  public:
    future<> start(unsigned shard_id, const db_config &cfg,
                   pool::ShardMemoryManager &memory_manager);
    future<> stop();

    /**
     * VSET <index> <key> <key-vector> <value>
     */

    future<bool> vset(std::string_view index, std::string_view key,
                      std::vector<float> embedding, std::string value,
                      centroid_id centroid);
    future<bool> vset_brute(std::string_view index, std::string_view key,
                            std::vector<float> embedding, std::string value);
    future<bool> vdelete(std::string_view index, std::string_view key);
    future<std::optional<sstring>> vget(std::string_view index,
                                        std::string_view key);

    /**
     * VSEARCH <index> <vector>
     */
    future<std::vector<VectorSearchResult>>
    vsearch(std::string_view index, std::vector<float> query_embedding,
            uint32_t top_k, centroid_id id);
    future<std::vector<VectorSearchResult>>
    vsearch_brute(std::string_view index, std::vector<float> query_embedding,
                  uint32_t top_k);

    bool check_if_key_exists(std::string_view key, std::string_view index);

    bool is_index_enabled() const { return index_enabled_; }
    bool is_index_building() const { return index_building_; }

    future<std::vector<LocalCentroidSnapshot>> build_local_index();
    std::size_t local_entry_count() const;
    VectorStoreInfoSnapshot snapshot_info() const;

  private:
    absl::flat_hash_map<vector_index_t, std::unique_ptr<VectorIndex>>
        vector_index_map_;
    bool index_enabled_ = false; // Replicated variable
    bool index_building_ = false;
    uint64_t index_version_ = 0;
    uint32_t centroid_group_count_ = 5;
};

} // namespace shunyakv
