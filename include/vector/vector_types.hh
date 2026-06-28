#pragma once

#include <absl/container/flat_hash_set.h>
#include <queue>
#include <seastar/core/sstring.hh>
#include <seastar/core/temporary_buffer.hh>
#include <unordered_set>

namespace shunyakv {

using vector_index_t = seastar::sstring;
using centroid_id = uint32_t;
using scatter_result = std::unordered_map<shard_id, unordered_set<centroid_id>>;
using VectorResultQueue =
    std::priority_queue<VectorSearchResult, std::vector<VectorSearchResult>,
                        std::greater<VectorSearchResult>>;
struct VectorSearchResult {
    float score;
    std::string key;
    std::string value;

    bool operator>(const VectorSearchResult &other) const noexcept {
        return score > other.score;
    }
};
struct CentroidBucket {
    centroid_id id = 0;
    std::vector<float> centroid_vector;
    absl::flat_hash_set<key_t> member_keys;
};

enum class distance_metric { cosine, dot_product, l2 };

struct VectorIndexConfig {
    uint32_t dim = 0;
    distance_metric metric = distance_metric::cosine;
};
struct Centroid {
    centroid_id id;
    std::vector<float> embedding;
};

struct CentroidScore {
    float score;
    centroid_id id;
    shard_id target_shard_id;

    bool operator>(const CentroidScore &other) const {
        return score > other.score;
    }
};

struct CentroidTable {
    std::vector<Centroid> centroids;
    std::unordered_map<centroid_id, shard_id> centroid_shard_mapping_;
    uint64_t version = 0;
};
} // namespace shunyakv
