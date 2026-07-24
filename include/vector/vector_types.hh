#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <optional>
#include <queue>
#include <seastar/core/shard_id.hh>
#include <seastar/core/sstring.hh>
#include <seastar/core/temporary_buffer.hh>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "kv_types.hh"

namespace shunyakv {

using vector_index_t = seastar::sstring;
using centroid_id = uint32_t;

struct GenerationId {
    uint64_t index = 0;
    uint64_t generation = 0;

    bool operator==(const GenerationId &) const noexcept = default;

    template <typename H> friend H AbslHashValue(H h, const GenerationId &id) {
        return H::combine(std::move(h), id.index, id.generation);
    }
};

using scatter_result =
    std::unordered_map<seastar::shard_id, std::unordered_set<centroid_id>>;
struct GlobalCentroidId {
    seastar::shard_id shard_id;
    centroid_id local_centroid_id;

    bool operator==(const GlobalCentroidId &other) const noexcept {
        return shard_id == other.shard_id &&
               local_centroid_id == other.local_centroid_id;
    }
};

struct GlobalCentroidIdHash {
    std::size_t operator()(const GlobalCentroidId &id) const noexcept {
        return std::hash<seastar::shard_id>{}(id.shard_id) ^
               (std::hash<centroid_id>{}(id.local_centroid_id) << 1);
    }
};

struct VectorSearchResult {
    float score;
    std::string key;
    std::string value;

    bool operator>(const VectorSearchResult &other) const noexcept {
        return score > other.score;
    }
};
using VectorResultQueue =
    std::priority_queue<VectorSearchResult, std::vector<VectorSearchResult>,
                        std::greater<VectorSearchResult>>;

struct HnswIndex {
    // node_id -> level -> neighbour node_ids
    std::vector<std::vector<std::vector<std::size_t>>> neighbours;
    // node_id -> real key
    std::vector<GenerationId> member_keys;
    std::optional<std::size_t> entry_point;
    std::size_t m = 24;
    std::size_t ef_construction = 256;
    std::size_t ef_search = 32;
    std::size_t max_level = 0;
    std::vector<bool> deleted;
    absl::flat_hash_map<GenerationId, std::size_t> key_to_node;
    std::size_t tombstone_count = 0;
};

// Compatibility representation for the original VectorIndex, which owns
// entries by key instead of addressing them through VectorStorageManager.
struct LegacyHnswIndex {
    std::vector<std::vector<std::vector<std::size_t>>> neighbours;
    std::vector<key_t> member_keys;
    std::optional<std::size_t> entry_point;
    std::size_t m = 8;
    std::size_t ef_construction = 64;
    std::size_t max_level = 0;
    std::vector<bool> deleted;
    absl::flat_hash_map<key_t, std::size_t> key_to_node;
    std::size_t tombstone_count = 0;
};

struct CentroidBucket {
    centroid_id id = 0;
    std::vector<float> centroid_vector;
    absl::flat_hash_set<key_t> member_keys;

    LegacyHnswIndex hnsw_index;
};

enum class distance_metric { cosine, dot_product, l2 };

enum class State { ACCUMULATING, BUILDING_HNSW, HNSW_ACTIVE, HNSW_MERGING };

struct VectorIndexConfig {
    uint32_t dim = 0;
    distance_metric metric = distance_metric::cosine;
};
struct Centroid {
    GlobalCentroidId id;
    std::vector<float> embedding;
};

struct CentroidScore {
    float score;
    centroid_id id;
    seastar::shard_id target_shard_id;

    bool operator>(const CentroidScore &other) const {
        return score > other.score;
    }
};

struct VectorPoint {
    centroid_id id;
    seastar::shard_id target_shard_id;
};

struct CentroidTable {
    std::vector<Centroid> centroids;
    uint64_t routing_version = 0;
    std::unordered_map<seastar::shard_id, uint64_t> shard_snapshot_versions;
};

struct LocalCentroidSnapshot {
    vector_index_t index;
    uint64_t version;
    uint32_t dim;
    std::vector<Centroid> centroids;
};
} // namespace shunyakv
