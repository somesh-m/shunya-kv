#pragma once

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "kv_types.hh"
#include "vector/helper.hh"
#include "vector/search_result.hh"
#include "vector/vector_entry.hh"
#include "vector/vector_types.hh"
#include <algorithm>
#include <functional>
#include <queue>
#include <random>
#include <seastar/coroutine/maybe_yield.hh>
#include <stdexcept>

namespace shunyakv {

class VectorIndex {
  private:
    VectorIndexConfig _config;
    seastar::sstring index_;
    uint32_t version_ = 0;

    absl::flat_hash_map<key_t, VectorEntry> _entries;
    absl::flat_hash_map<centroid_id, CentroidBucket> _centroid_buckets;

  public:
    explicit VectorIndex(VectorIndexConfig config, seastar::sstring index)
        : _config(config), index_(index) {}

    const VectorIndexConfig &config() const { return _config; }

    bool exists(const key_t &key) const {
        return _entries.find(key) != _entries.end();
    }

    std::size_t size() const { return _entries.size(); }

    std::vector<std::vector<float>> sample_random_vectors(size_t k) const {
        std::vector<std::vector<float>> samples;
        samples.reserve(k);

        std::mt19937_64 rng{std::random_device{}()};

        size_t seen = 0;

        for (const auto &[key, entry] : _entries) {
            ++seen;

            if (samples.size() < k) {
                samples.push_back(entry.embedding);
                continue;
            }

            std::uniform_int_distribution<size_t> dist(0, seen - 1);
            size_t j = dist(rng);

            if (j < k) {
                samples[j] = entry.embedding;
            }
        }

        return samples;
    }

    bool validate_dim(const std::vector<float> &embedding) const {
        return embedding.size() == _config.dim;
    }

    std::size_t total_entry_count() const { return _entries.size(); }
    std::size_t total_centroid_count() const {
        return _centroid_buckets.size();
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

    seastar::future<std::optional<LocalCentroidSnapshot>>
    build_local_index(uint32_t centroid_group_count) {
        constexpr std::size_t kYieldEvery = 32;
        absl::flat_hash_map<centroid_id, CentroidBucket> centroid_bucket;
        std::vector<std::pair<key_t, const VectorEntry *>> entries;
        entries.reserve(_entries.size());

        for (const auto &[key, entry] : _entries) {
            entries.emplace_back(key, &entry);
        }

        std::vector<std::vector<float>> inter_centroids =
            sample_random_vectors(centroid_group_count);

        if (inter_centroids.empty()) {
            co_return std::nullopt;
        }

        for (uint32_t i = 0; i < inter_centroids.size(); i++) {
            centroid_bucket[i] = CentroidBucket{
                .id = i,
                .centroid_vector = inter_centroids[i],
            };
        }

        // Hardcoding dimension, we need to make this constant at an index level
        // based on the first entry in that index.
        uint32_t dim = inter_centroids[0].size();

        // Hardcoded 10 iterations of centroid rebuilding for now
        for (uint32_t i = 0; i < 10; i++) {
            std::vector<centroid_id> centroid_ids;
            centroid_ids.reserve(centroid_bucket.size());
            for (auto &[id, bucket] : centroid_bucket) {
                bucket.member_keys.clear();
                centroid_ids.push_back(id);
            }

            // Assign vectors to their nearest centroids
            std::size_t assigned_vectors = 0;
            for (const auto &[key, entry] : entries) {
                float max_score = -1.0f;
                uint32_t max_score_index = 0;

                for (uint32_t centroid_id = 0;
                     centroid_id < inter_centroids.size(); centroid_id++) {
                    const float score = ::vdb::find_cosine_similarity(
                        entry->embedding, inter_centroids[centroid_id]);

                    if (score > max_score) {
                        max_score_index = centroid_id;
                        max_score = score;
                    }
                }

                centroid_bucket[max_score_index].member_keys.insert(key);
                if (++assigned_vectors % kYieldEvery == 0) {
                    co_await seastar::coroutine::maybe_yield();
                }
            }

            // Recompute centroids
            std::size_t centroid_index = 0;
            for (const auto centroid_id : centroid_ids) {
                if (centroid_index % kYieldEvery == 0) {
                    co_await seastar::coroutine::maybe_yield();
                }
                ++centroid_index;

                auto it = centroid_bucket.find(centroid_id);
                if (it == centroid_bucket.end()) {
                    continue;
                }

                uint32_t member_key_count = it->second.member_keys.size();
                if (member_key_count == 0) {
                    continue;
                }

                std::vector<float> new_centroid(dim, 0.0f);

                for (const auto &key : it->second.member_keys) {
                    auto entry_it = _entries.find(key);
                    if (entry_it == _entries.end()) {
                        continue;
                    }

                    const auto &current_vector = entry_it->second.embedding;

                    for (uint32_t d = 0; d < dim; d++) {
                        new_centroid[d] += current_vector[d];
                    }
                }

                for (uint32_t d = 0; d < dim; d++) {
                    new_centroid[d] /= member_key_count;
                }

                ::vdb::normalize(new_centroid);

                inter_centroids[centroid_id] = new_centroid;
                it->second.centroid_vector = new_centroid;
            }
        }

        _centroid_buckets = std::move(centroid_bucket);
        /**
         * Vector Entry contains centroid information as well, we need to
         * update that too. This mapping is needed for certain use cases like
         * Delete a key
         * In case of update if the entry changes the centroid
         **/
        std::vector<centroid_id> centroid_ids;
        centroid_ids.reserve(_centroid_buckets.size());
        for (const auto &[centroid_id, _] : _centroid_buckets) {
            centroid_ids.push_back(centroid_id);
        }

        std::size_t centroid_index = 0;
        for (const auto centroid_id : centroid_ids) {
            if (centroid_index % kYieldEvery == 0) {
                co_await seastar::coroutine::maybe_yield();
            }
            ++centroid_index;

            auto bucket_it = _centroid_buckets.find(centroid_id);
            if (bucket_it == _centroid_buckets.end()) {
                continue;
            }

            const auto &bucket = bucket_it->second;
            for (const auto &key : bucket.member_keys) {
                auto entry_it = _entries.find(key);
                if (entry_it == _entries.end()) {
                    continue;
                }

                entry_it->second.centroid = centroid_id;
            }
        }
        version_++;
        co_return export_centroid_snapshot();
    }

    bool has_trained_centroids() const { return !_centroid_buckets.empty(); }

    uint64_t centroid_version() const { return version_; }

    std::optional<LocalCentroidSnapshot> export_centroid_snapshot() const {
        if (_centroid_buckets.empty()) {
            return std::nullopt;
        }

        std::vector<Centroid> centroids;
        centroids.reserve(_centroid_buckets.size());

        const seastar::shard_id shard_id = seastar::this_shard_id();

        for (const auto &[key, entry] : _centroid_buckets) {
            centroids.push_back(Centroid{
                .id =
                    GlobalCentroidId{
                        .shard_id = shard_id,
                        .local_centroid_id = key,
                    },
                .embedding = entry.centroid_vector,
            });
        }

        return LocalCentroidSnapshot{
            .index = index_,
            .version = version_,
            .dim = _config.dim,
            .centroids = std::move(centroids),
        };
    }

    uint32_t dim() const { return _config.dim; }
};

} // namespace shunyakv
