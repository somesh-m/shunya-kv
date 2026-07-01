#pragma once
#include "vector/helper.hh"
#include "vector/vector_types.hh"
#include <cstdint>
#include <functional>
#include <optional>
#include <queue>
#include <seastar/core/smp.hh>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * It is easy to confuse vector routing with the local vector index.
 *
 * vector_routing:
 *   - Maintains the mapping between centroids and their owning shards.
 *   - Used to route VSET and VSEARCH requests to the appropriate shard(s).
 *   - Does not own any vector data.
 *
 * vector_index:
 *   - Stores the actual vectors and their associated values.
 *   - Maintains the mapping between keys and embeddings.
 *   - Maintains the mapping between centroids and member keys.
 *   - Executes brute-force and indexed (IVF) search locally on a shard.
 */

namespace vdb {
using centroid_id = uint32_t;
using shard_id = seastar::shard_id;

class VDBOrchestrator {
  public:
    VDBOrchestrator() = default;

    void add_centroid(std::vector<float> embedding, std::string_view index) {
        auto &routing_table =
            index_table_[shunyakv::vector_index_t(index.data(), index.size())];
        centroid_id id = routing_table.centroids.size() + 1;
        auto cent = shunyakv::Centroid{
            .id =
                shunyakv::GlobalCentroidId{
                    .shard_id = choose_owner(id),
                    .local_centroid_id = id,
                },
            .embedding = std::move(embedding),
        };
        routing_table.centroids.push_back(std::move(cent));
    }

    void replace_index_centroids(
        const shunyakv::LocalCentroidSnapshot &snapshot) {
        if (snapshot.centroids.empty()) {
            return;
        }

        auto &routing_table = index_table_[snapshot.index];
        std::unordered_set<seastar::shard_id> shard_ids;
        const auto shard_id = snapshot.centroids.front().id.shard_id;

        const auto current_it =
            routing_table.shard_snapshot_versions.find(shard_id);
        if (current_it != routing_table.shard_snapshot_versions.end() &&
            current_it->second >= snapshot.version) {
            return;
        }

        shard_ids.insert(shard_id);
        routing_table.centroids.erase(
            std::remove_if(
                routing_table.centroids.begin(), routing_table.centroids.end(),
                [&](const shunyakv::Centroid &centroid) {
                    return shard_ids.contains(centroid.id.shard_id);
                }),
            routing_table.centroids.end());

        routing_table.centroids.insert(routing_table.centroids.end(),
                                       snapshot.centroids.begin(),
                                       snapshot.centroids.end());
        routing_table.shard_snapshot_versions[shard_id] = snapshot.version;
        routing_table.routing_version++;
    }

    std::optional<shard_id>
    find_vector_owner_shard(std::span<const float> embedding,
                            std::string_view index) {
        const auto it = index_table_.find(
            shunyakv::vector_index_t(index.data(), index.size()));
        if (it == index_table_.end() || it->second.centroids.empty()) {
            return std::nullopt;
        }

        const auto &routing_table = it->second;
        std::optional<shunyakv::GlobalCentroidId> nearest_centroid;
        float best_score = -1.0f; // cosine range: [-1, 1]

        for (const auto &centroid : routing_table.centroids) {
            float score = find_cosine_similarity(centroid.embedding, embedding);

            if (score > best_score) {
                best_score = score;
                nearest_centroid = centroid.id;
            }
        }

        if (!nearest_centroid) {
            return std::nullopt;
        }

        return nearest_centroid->shard_id;
    }

    std::vector<shunyakv::CentroidScore>
    find_top_centroids(std::span<const float> query_embedding,
                       std::string_view index) {
        std::vector<shunyakv::CentroidScore> result;
        auto it = index_table_.find(
            shunyakv::vector_index_t(index.data(), index.size()));

        if (it == index_table_.end()) {
            return result;
        }

        const auto &routing_table = it->second;

        std::priority_queue<shunyakv::CentroidScore,
                            std::vector<shunyakv::CentroidScore>,
                            std::greater<shunyakv::CentroidScore>>
            winning_centroids;

        for (const auto &centroid : routing_table.centroids) {
            float score =
                find_cosine_similarity(centroid.embedding, query_embedding);

            winning_centroids.push(shunyakv::CentroidScore{
                .score = score,
                .id = centroid.id.local_centroid_id,
                .target_shard_id = centroid.id.shard_id,
            });

            if (winning_centroids.size() > nprobe_) {
                winning_centroids.pop();
            }
        }

        result.reserve(winning_centroids.size());

        while (!winning_centroids.empty()) {
            auto item = winning_centroids.top();
            winning_centroids.pop();

            result.push_back(item);
        }

        return result;
    }

    size_t nprobe() const { return nprobe_; }

  private:
    std::unordered_map<shunyakv::vector_index_t, shunyakv::CentroidTable>
        index_table_;
    size_t nprobe_ = 5;

    static shard_id choose_owner(centroid_id id) {
        return id % seastar::smp::count;
    }
};
} // namespace vdb
