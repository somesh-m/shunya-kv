#pragma once
#include "dbconfig.hh"
#include "eviction/sieve_policy.hh"
#include "pool/object_pool.hh"
#include "shard_stats.hh"
#include "vector_entry.hh"
#include "vector_index.hh"
#include "vector_types.hh"
#include <absl/container/flat_hash_map.h>
#include <optional>
#include <queue>
#include <seastar/core/future.hh>
#include <string_view>

/**
 * Each shard is supposed to have it's own copy of vector store
 */

using namespace seastar;
namespace shunyakv {
class VectorStore {

  public:
    future<> start(unsigned shard_id, const db_config &cfg,
                   ShardMemoryManager &memory_manager);
    future<> stop();

    /**
     * VSET <index> <key> <key-vector> <value>
     */

    future<bool> vset(std::string_view index, std::string_view key,
                      std::vector<float> embedding, std::string value);

    /**
     * VSEARCH <index> <vector>
     */
    future<std::optional<std::string>>
    vsearch(std::string_view index, std::vector<float> query_embedding);

  private:
    absl::flat_hash_map<vector_index_t, std::unique_ptr<VectorIndex>>
        vector_index_map_;
    std::optional<EntryPool> entry_pool_;
}

} // namespace shunyakv
