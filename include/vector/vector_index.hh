#pragma once

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vector/helper.hh"
#include <algorithm>
#include <functional>
#include <queue>
#include <stdexcept>
#include "kv_types.hh"
#include "vector/search_result.hh"
#include "vector/vector_entry.hh"
#include "vector/vector_types.hh"

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

    void upsert_brute(key_t key, std::vector<float> embedding,
                      std::string value) {
        if (!validate_dim(embedding)) {
            throw std::runtime_error("VECTOR_DIMENSION_MISMATCH");
        }

        auto old_entry = _entries.find(key);

        if (old_entry != _entries.end()) {
            old_entry->second.value = std::move(value);
            old_entry->second.embedding = std::move(embedding);
            old_entry->second.ver++;
            return;
        }

        _entries.emplace(key, VectorEntry{
                                  key, std::move(value), std::move(embedding),
                                  centroid_id{0} // unused in brute-force mode
                              });
    }

    void upsert(key_t key, std::vector<float> embedding, std::string value,
                centroid_id new_centroid) {
        if (!validate_dim(embedding)) {
            throw std::runtime_error("VECTOR_DIMENSION_MISMATCH");
        }

        auto old_entry = _entries.find(key);

        if (old_entry != _entries.end()) {
            centroid_id old_centroid = old_entry->second.centroid;

            if (old_centroid != new_centroid) {
                _centroid_buckets[old_centroid].member_keys.erase(key);

                _centroid_buckets[new_centroid].id = new_centroid;
                _centroid_buckets[new_centroid].member_keys.insert(key);
            }

            old_entry->second.value = std::move(value);
            old_entry->second.embedding = std::move(embedding);
            old_entry->second.centroid = new_centroid;
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

        centroid_id centroid = entry_it->second.centroid;

        auto centroid_it = _centroid_buckets.find(centroid);
        if (centroid_it != _centroid_buckets.end()) {
            centroid_it->second.member_keys.erase(key);
        }

        _entries.erase(entry_it);

        return true;
    }

    std::vector<VectorSearchResult> search(const std::vector<float> &query,
                                           uint32_t top_k,
                                           centroid_id centroid_id) const {
        if (!validate_dim(query)) {
            throw std::runtime_error("VECTOR_DIMENSION_MISMATCH");
        }

        if (top_k == 0) {
            return {};
        }

        VectorResultQueue winners;

        const auto centroid_it = _centroid_buckets.find(centroid_id);

        if (centroid_it == _centroid_buckets.end()) {
            throw std::runtime_error("INVALID_CENTROID");
        }

        // Avoid copying all member keys.
        const auto &member_keys = centroid_it->second.member_keys;

        for (const auto &key : member_keys) {
            const auto entry_it = _entries.find(key);

            // Typo fixed: _entries, not _enries.
            if (entry_it == _entries.end()) {
                continue;
            }

            const auto &entry = entry_it->second;

            // Avoid copying the embedding.
            const float score =
                ::vdb::find_cosine_similarity(query, entry.embedding);

            winners.push(VectorSearchResult{
                .score = score,
                .key = std::string{key},
                .value = entry.value,
            });

            if (winners.size() > top_k) {
                // Min-heap: removes the lowest-scoring retained result.
                winners.pop();
            }
        }

        std::vector<VectorSearchResult> results;
        results.reserve(winners.size());

        // The min-heap returns the lowest retained score first.
        while (!winners.empty()) {
            results.push_back(winners.top());
            winners.pop();
        }

        // Return highest similarity first.
        std::reverse(results.begin(), results.end());

        return results;
    }

    std::vector<VectorSearchResult>
    search_brute(const std::vector<float> &query, uint32_t top_k) const {
        if (!validate_dim(query)) {
            throw std::runtime_error("VECTOR_DIMENSION_MISMATCH");
        }

        if (top_k == 0) {
            return {};
        }

        VectorResultQueue winners;

        for (const auto &[key, entry] : _entries) {
            const float score =
                ::vdb::find_cosine_similarity(query, entry.embedding);

            winners.push(VectorSearchResult{
                .score = score,
                .key = std::string{key},
                .value = entry.value,
            });

            if (winners.size() > top_k) {
                winners.pop();
            }
        }

        std::vector<VectorSearchResult> results;
        results.reserve(winners.size());

        while (!winners.empty()) {
            results.push_back(winners.top());
            winners.pop();
        }

        std::reverse(results.begin(), results.end());

        return results;
    }
};

} // namespace shunyakv
