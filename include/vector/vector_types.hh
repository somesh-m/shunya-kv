#pragma once

#include <absl/container/flat_hash_set.h>
#include <seastar/core/sstring.hh>
#include <seastar/core/temporary_buffer.hh>
#include <unordered_set>

namespace shunyakv {

using vector_index_t = seastar::sstring;
using centroid_id = uint32_t;

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
} // namespace shunyakv
