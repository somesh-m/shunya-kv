#pragma once

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <kv_types.hh>
#include <search_result.hh>
#include <vector_entry.hh>
#include <vector_types.hh>

namespace shunyakv {

class VectorIndex {
  private:
    VectorIndexConfig _config;

    absl::flat_hash_map<key_t, VectorEntry> _entries;
    absl::flat_hash_map<centroid_id, CentroidBucket> _centroid_buckets;

  public:
    explicit VectorIndex(VectorIndexConfig config) : _config(config) {}

    const VectorIndexConfig &config() const { return _config; }

    bool exists(const key_t &key) const {
        return _entries.find(key) != _entries.end();
    }

    size_t size() const { return _entries.size(); }

    bool validate_dim(const std::vector<float> &embedding) const {
        return embedding.size() == _config.dim;
    }

    centroid_id choose_centroid(const std::vector<float> &embedding) const {
        // TODO:
        // IVF path: find nearest centroid_vector.
        // Hash path: choose centroid based on key.
        return 0;
    }

    void upsert(key_t key, std::vector<float> embedding, std::string value) {
        if (!validate_dim(embedding)) {
            throw std::runtime_error("VECTOR_DIMENSION_MISMATCH");
        }

        centroid_id new_centroid = choose_centroid(embedding);

        auto old_entry = _entries.find(key);

        if (old_entry != _entries.end()) {
            centroid_id old_centroid = old_entry->second.centroid_id;

            if (old_centroid != new_centroid) {
                _centroid_buckets[old_centroid].member_keys.erase(key);

                _centroid_buckets[new_centroid].id = new_centroid;
                _centroid_buckets[new_centroid].member_keys.insert(key);
            }

            old_entry->second.value = std::move(value);
            old_entry->second.embedding = std::move(embedding);
            old_entry->second.centroid_id = new_centroid;
            old_entry->second.ver++;

            return;
        }

        _centroid_buckets[new_centroid].id = new_centroid;
        _centroid_buckets[new_centroid].member_keys.insert(key);

        _entries.emplace(key, VectorEntry{key, std::move(value),
                                          std::move(embedding), new_centroid});
    }

    const VectorEntry *get(const key_t &key) const {
        auto it = _entries.find(key);

        if (it == _entries.end()) {
            return nullptr;
        }

        return &it->second;
    }

    bool erase(const key_t &key) {
        auto entry_it = _entries.find(key);

        if (entry_it == _entries.end()) {
            return false;
        }

        centroid_id centroid = entry_it->second.centroid_id;

        auto centroid_it = _centroid_buckets.find(centroid);
        if (centroid_it != _centroid_buckets.end()) {
            centroid_it->second.member_keys.erase(key);
        }

        _entries.erase(entry_it);

        return true;
    }

    std::vector<VectorSearchResult> search(const std::vector<float> &query,
                                           uint32_t top_k) const {
        if (!validate_dim(query)) {
            throw std::runtime_error("VECTOR_DIMENSION_MISMATCH");
        }

        // TODO:
        // 1. choose nprobe centroids
        // 2. scan member_keys inside those centroids
        // 3. compute similarity score against _entries[key].embedding
        // 4. keep top_k
        // 5. return vector of {key, score}

        return {};
    }
};

} // namespace shunyakv
