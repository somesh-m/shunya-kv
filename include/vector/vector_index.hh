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
#include <limits>
#include <queue>
#include <random>
#include <seastar/coroutine/maybe_yield.hh>
#include <stdexcept>

namespace shunyakv {

class VectorIndex {
  private:
    using scored_node = std::pair<std::size_t, float>;

    VectorIndexConfig _config;
    seastar::sstring index_;
    uint32_t version_ = 0;

    // key string -> VectorEntry mapping.
    absl::flat_hash_map<key_t, VectorEntry> _entries;
    // Centroid bucket also contains reference to the hnsw index
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

        uint32_t next_version = 0;
        if (exists(key)) {
            next_version = _entries.find(key)->second.ver + 1;
            delete_vector(key);
        }

        auto &bucket = _centroid_buckets[new_centroid];
        bucket.id = new_centroid;
        bucket.member_keys.insert(key);

        auto [entry_it, _] = _entries.emplace(
            key, VectorEntry{key, std::move(value), std::move(embedding),
                             new_centroid});
        entry_it->second.ver = next_version;

        // Rebuild this centroid after the new entry exists in _entries.
        insert_into_hnsw_index(key, bucket.hnsw_index);
    }

    const VectorEntry *get(const key_t &key) const {
        auto it = _entries.find(key);

        if (it == _entries.end()) {
            return nullptr;
        }

        return &it->second;
    }

    bool erase(const key_t &key) {
        if (!exists(key)) {
            return false;
        }

        delete_vector(key);
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

        const auto &bucket = centroid_it->second;
        const auto &hnsw_index = bucket.hnsw_index;
        bool used_hnsw = false;

        if (hnsw_index.entry_point.has_value() &&
            is_valid_hnsw_node(*hnsw_index.entry_point, hnsw_index) &&
            !hnsw_index.key_to_node.empty()) {
            std::size_t ep = *hnsw_index.entry_point;

            for (std::size_t level = hnsw_index.max_level; level > 0; --level) {
                ep = greedy_search_vector(query, ep, level, hnsw_index);
            }

            const std::size_t ef =
                std::max<std::size_t>(top_k, hnsw_index.ef_construction);
            const auto candidates =
                search_layer_vector(query, ep, ef, 0, hnsw_index);

            for (const auto &[node_id, score] : candidates) {
                if (is_deleted_hnsw_node(node_id, hnsw_index) ||
                    !is_valid_hnsw_node(node_id, hnsw_index)) {
                    continue;
                }

                const key_t &key = hnsw_index.member_keys[node_id];
                const auto entry_it = _entries.find(key);
                if (entry_it == _entries.end()) {
                    continue;
                }

                winners.push(VectorSearchResult{
                    .score = score,
                    .key = std::string{key},
                    .value = entry_it->second.value,
                });

                if (winners.size() > top_k) {
                    winners.pop();
                }
                used_hnsw = true;
            }
        }

        if (!used_hnsw) {
            for (const auto &key : bucket.member_keys) {
                const auto entry_it = _entries.find(key);
                if (entry_it == _entries.end()) {
                    continue;
                }

                const auto &entry = entry_it->second;
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

        centroid_index = 0;
        for (const auto centroid_id : centroid_ids) {
            if (centroid_index % kYieldEvery == 0) {
                co_await seastar::coroutine::maybe_yield();
            }
            ++centroid_index;
            rebuild_hnsw_index(centroid_id);
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

    void insert_into_hnsw_index(const key_t &key, HnswIndex &index) {
        auto entry_it = _entries.find(key);
        if (entry_it == _entries.end()) {
            return;
        }

        // Avoid duplicate live node for same key.
        // In normal SET flow, delete_vector(key) should erase this mapping
        // before reinserting.
        if (index.key_to_node.find(key) != index.key_to_node.end()) {
            return;
        }

        const auto &vector = entry_it->second.embedding;

        const std::size_t new_level = random_level(index.m, 8);
        const std::size_t new_id = index.member_keys.size();

        index.member_keys.push_back(key);

        // Reverse mapping: key -> local HNSW node id.
        index.key_to_node[key] = new_id;

        // This node is live.
        index.deleted.push_back(false);

        // node_id -> levels -> neighbour node ids
        index.neighbours.emplace_back(new_level + 1);

        // If this is the first live node, make it the entry point.
        //
        // key_to_node.size() == 1 means this is the only live key in this HNSW.
        // This also handles the case where older nodes exist but are
        // soft-deleted.
        if (!index.entry_point.has_value() || index.key_to_node.size() == 1) {
            index.entry_point = new_id;
            index.max_level = new_level;
            return;
        }

        std::size_t ep = index.entry_point.value();

        for (std::size_t level = index.max_level; level > new_level; --level) {
            ep = greedy_search_vector(vector, ep, level, index);
        }

        const std::size_t start_level = std::min(new_level, index.max_level);

        for (std::size_t level = start_level + 1; level-- > 0;) {
            auto candidates = search_layer_vector(
                vector, ep, index.ef_construction, level, index);

            auto selected = select_top_m(candidates, index.m);

            for (std::size_t neighbour : selected) {
                connect_bidirectional(new_id, neighbour, level, index);
            }

            if (!candidates.empty()) {
                ep = candidates.front().first;
            }
        }

        if (new_level > index.max_level) {
            index.entry_point = new_id;
            index.max_level = new_level;
        }
    }

    void connect_bidirectional(std::size_t a, std::size_t b, std::size_t level,
                               HnswIndex &index) {
        index.neighbours[a][level].push_back(b);
        index.neighbours[b][level].push_back(a);

        prune_neighbours(a, level, index);
        prune_neighbours(b, level, index);
    }

    bool check_if_key_exists(std::string_view key) {
        if (_entries.find(key_t{key}) == _entries.end()) {
            return false;
        }
        return true;
    }

    bool is_valid_hnsw_node(std::size_t node_id, const HnswIndex &index) const {
        return node_id < index.member_keys.size() &&
               node_id < index.neighbours.size();
    }

    bool is_deleted_hnsw_node(std::size_t node_id,
                              const HnswIndex &index) const {
        return node_id < index.deleted.size() && index.deleted[node_id];
    }

    bool has_hnsw_level(std::size_t node_id, std::size_t level,
                        const HnswIndex &index) const {
        return is_valid_hnsw_node(node_id, index) &&
               level < index.neighbours[node_id].size();
    }

    float score_hnsw_node(const std::vector<float> &query, std::size_t node_id,
                          const HnswIndex &index) const {
        if (!is_valid_hnsw_node(node_id, index) ||
            is_deleted_hnsw_node(node_id, index)) {
            return -std::numeric_limits<float>::infinity();
        }

        const key_t &key = index.member_keys[node_id];

        auto entry_it = _entries.find(key);
        if (entry_it == _entries.end()) {
            return -std::numeric_limits<float>::infinity();
        }

        // Replace this with your actual scoring function if named differently.
        // Higher should be better.
        return ::vdb::find_cosine_similarity(query, entry_it->second.embedding);
    }

    std::size_t greedy_search_vector(const std::vector<float> &query,
                                     std::size_t entry_point, std::size_t level,
                                     const HnswIndex &index) const {
        if (!is_valid_hnsw_node(entry_point, index)) {
            return entry_point;
        }

        std::size_t current = entry_point;
        float current_score = score_hnsw_node(query, current, index);

        bool changed = true;

        while (changed) {
            changed = false;

            if (!has_hnsw_level(current, level, index)) {
                break;
            }

            const auto &neighbours = index.neighbours[current][level];

            for (std::size_t neighbour : neighbours) {
                if (!is_valid_hnsw_node(neighbour, index)) {
                    continue;
                }

                if (!has_hnsw_level(neighbour, level, index)) {
                    continue;
                }

                const float neighbour_score =
                    score_hnsw_node(query, neighbour, index);

                if (neighbour_score > current_score) {
                    current = neighbour;
                    current_score = neighbour_score;
                    changed = true;
                }
            }
        }
        return current;
    }

    std::vector<std::size_t> select_top_m(std::vector<scored_node> candidates,
                                          std::size_t m) const {
        std::vector<std::size_t> selected;

        if (m == 0 || candidates.empty()) {
            return selected;
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const scored_node &a, const scored_node &b) {
                      return a.second > b.second;
                  });

        const std::size_t limit = std::min(m, candidates.size());
        selected.reserve(limit);

        for (std::size_t i = 0; i < limit; ++i) {
            selected.push_back(candidates[i].first);
        }

        return selected;
    }

    bool delete_from_hnsw_index(const key_t &key, HnswIndex &index) {
        auto node_it = index.key_to_node.find(key);

        if (node_it == index.key_to_node.end()) {
            return false;
        }

        const std::size_t node_id = node_it->second;

        if (node_id < index.deleted.size() && !index.deleted[node_id]) {
            index.deleted[node_id] = true;
            index.tombstone_count++;
        }

        index.key_to_node.erase(node_it);

        return true;
    }

    void rebuild_hnsw_index(centroid_id cid) {
        auto bucket_it = _centroid_buckets.find(cid);

        if (bucket_it == _centroid_buckets.end()) {
            return;
        }

        auto &bucket = bucket_it->second;
        auto &index = bucket.hnsw_index;

        std::vector<key_t> live_keys;
        live_keys.reserve(bucket.member_keys.size());

        for (const key_t &key : bucket.member_keys) {
            auto entry_it = _entries.find(key);
            if (entry_it == _entries.end()) {
                continue;
            }

            if (entry_it->second.centroid != cid) {
                continue;
            }

            live_keys.push_back(key);
        }

        const std::size_t old_m = index.m;
        const std::size_t old_ef_construction = index.ef_construction;

        index.neighbours.clear();
        index.member_keys.clear();
        index.key_to_node.clear();
        index.deleted.clear();
        index.entry_point.reset();
        index.max_level = 0;
        index.tombstone_count = 0;

        index.m = old_m;
        index.ef_construction = old_ef_construction;

        index.neighbours.reserve(live_keys.size());
        index.member_keys.reserve(live_keys.size());
        index.key_to_node.reserve(live_keys.size());
        index.deleted.reserve(live_keys.size());

        for (const key_t &key : live_keys) {
            insert_into_hnsw_index(key, index);
        }
    }

    void delete_vector(const key_t &key) {
        auto entry_it = _entries.find(key);

        if (entry_it == _entries.end()) {
            return;
        }

        const centroid_id cid = entry_it->second.centroid;

        auto bucket_it = _centroid_buckets.find(cid);

        bool deleted_entry_point = false;
        bool should_rebuild = false;

        if (bucket_it != _centroid_buckets.end()) {
            bucket_it->second.member_keys.erase(key);
            auto &index = bucket_it->second.hnsw_index;

            auto node_it = index.key_to_node.find(key);

            if (node_it != index.key_to_node.end()) {
                const std::size_t node_id = node_it->second;

                deleted_entry_point = index.entry_point.has_value() &&
                                      index.entry_point.value() == node_id;
            }

            delete_from_hnsw_index(key, index);

            should_rebuild =
                deleted_entry_point || should_rebuild_hnsw_index(index);
        }

        _entries.erase(entry_it);

        if (should_rebuild) {
            rebuild_hnsw_index(cid);
        }

        bucket_it = _centroid_buckets.find(cid);
        if (bucket_it != _centroid_buckets.end() &&
            bucket_it->second.member_keys.empty()) {
            _centroid_buckets.erase(bucket_it);
        }
    }

    bool should_rebuild_hnsw_index(const HnswIndex &index) const {
        const std::size_t total_nodes = index.member_keys.size();

        if (total_nodes == 0) {
            return false;
        }

        // if (index.tombstone_count < HNSW_MIN_TOMBSTONES_BEFORE_REBUILD) {
        //     return false;
        // }

        const double tombstone_ratio =
            static_cast<double>(index.tombstone_count) /
            static_cast<double>(total_nodes);

        return tombstone_ratio >= 0.2;
    }

    std::vector<scored_node>
    search_layer_vector(const std::vector<float> &query,
                        std::size_t entry_point, std::size_t ef,
                        std::size_t level, const HnswIndex &index) const {
        std::vector<scored_node> result;

        if (ef == 0) {
            return result;
        }

        if (entry_point >= index.neighbours.size()) {
            return result;
        }

        struct CandidateComparator {
            bool operator()(const scored_node &a, const scored_node &b) const {
                // Max heap: highest score first.
                return a.second < b.second;
            }
        };

        struct ResultComparator {
            bool operator()(const scored_node &a, const scored_node &b) const {
                // Min heap: lowest score first.
                // This lets us quickly remove the worst candidate.
                return a.second > b.second;
            }
        };

        std::priority_queue<scored_node, std::vector<scored_node>,
                            CandidateComparator>
            candidates;

        std::priority_queue<scored_node, std::vector<scored_node>,
                            ResultComparator>
            nearest;

        absl::flat_hash_set<std::size_t> visited;

        const float entry_score = score_hnsw_node(query, entry_point, index);

        candidates.push({entry_point, entry_score});
        visited.insert(entry_point);

        if (!is_deleted_hnsw_node(entry_point, index)) {
            nearest.push({entry_point, entry_score});
        }

        while (!candidates.empty()) {
            auto current = candidates.top();
            candidates.pop();

            if (!nearest.empty() && nearest.size() >= ef) {
                const float worst_nearest_score = nearest.top().second;

                // Since candidates is max-heap, if the best unexplored
                // candidate is worse than our worst nearest result, stop.
                if (current.second < worst_nearest_score) {
                    break;
                }
            }

            const std::size_t current_node = current.first;

            if (current_node >= index.neighbours.size()) {
                continue;
            }

            if (level >= index.neighbours[current_node].size()) {
                continue;
            }

            const auto &neighbours = index.neighbours[current_node][level];

            for (std::size_t neighbour : neighbours) {
                if (visited.contains(neighbour)) {
                    continue;
                }

                visited.insert(neighbour);

                if (neighbour >= index.neighbours.size()) {
                    continue;
                }

                const float neighbour_score =
                    score_hnsw_node(query, neighbour, index);

                const bool nearest_not_full = nearest.size() < ef;
                const bool better_than_worst =
                    nearest.empty() || neighbour_score > nearest.top().second;

                if (nearest_not_full || better_than_worst) {
                    candidates.push({neighbour, neighbour_score});

                    if (!is_deleted_hnsw_node(neighbour, index)) {
                        nearest.push({neighbour, neighbour_score});

                        if (nearest.size() > ef) {
                            nearest.pop();
                        }
                    }
                }
            }
        }

        while (!nearest.empty()) {
            result.push_back(nearest.top());
            nearest.pop();
        }

        std::sort(result.begin(), result.end(),
                  [](const scored_node &a, const scored_node &b) {
                      return a.second > b.second;
                  });

        return result;
    }

    std::size_t random_level(std::size_t m, std::size_t max_level) {
        if (m <= 1) {
            return 0;
        }

        static thread_local std::mt19937_64 rng{std::random_device{}()};

        std::uniform_real_distribution<double> dist(0.0, 1.0);

        const double probability = 1.0 / static_cast<double>(m);

        std::size_t level = 0;

        while (level < max_level && dist(rng) < probability) {
            ++level;
        }

        return level;
    }

    void prune_neighbours(std::size_t node_id, std::size_t level,
                          HnswIndex &index) {
        if (node_id >= index.neighbours.size()) {
            return;
        }

        if (level >= index.neighbours[node_id].size()) {
            return;
        }

        auto &neighbours = index.neighbours[node_id][level];

        if (neighbours.empty()) {
            return;
        }

        if (node_id >= index.member_keys.size()) {
            neighbours.clear();
            return;
        }

        const key_t &node_key = index.member_keys[node_id];

        auto node_entry_it = _entries.find(node_key);
        if (node_entry_it == _entries.end()) {
            neighbours.clear();
            return;
        }

        const auto &node_vector = node_entry_it->second.embedding;

        std::vector<scored_node> scored;
        scored.reserve(neighbours.size());

        absl::flat_hash_set<std::size_t> seen;

        for (std::size_t neighbour_id : neighbours) {
            if (neighbour_id == node_id) {
                continue;
            }

            if (seen.contains(neighbour_id)) {
                continue;
            }

            seen.insert(neighbour_id);

            if (neighbour_id >= index.neighbours.size()) {
                continue;
            }

            if (neighbour_id >= index.member_keys.size()) {
                continue;
            }

            if (neighbour_id < index.deleted.size() &&
                index.deleted[neighbour_id]) {
                continue;
            }

            const float score =
                score_hnsw_node(node_vector, neighbour_id, index);

            scored.push_back({neighbour_id, score});
        }

        if (scored.empty()) {
            neighbours.clear();
            return;
        }

        std::sort(scored.begin(), scored.end(),
                  [](const scored_node &a, const scored_node &b) {
                      return a.second > b.second;
                  });

        const std::size_t keep_count = std::min(index.m, scored.size());

        neighbours.clear();
        neighbours.reserve(keep_count);

        for (std::size_t i = 0; i < keep_count; ++i) {
            neighbours.push_back(scored[i].first);
        }
    }
};

} // namespace shunyakv
