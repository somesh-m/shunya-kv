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
            .id = id,
            .embedding = std::move(embedding),
        };
        shard_id owner_id = choose_owner(cent.id);
        routing_table.centroid_shard_mapping_[cent.id] = owner_id;
        routing_table.centroids.push_back(std::move(cent));
    }

    std::optional<shard_id>
    find_vector_owner_shard(std::span<const float> embedding,
                            std::string_view index) {
        const auto it =
            index_table_.find(shunyakv::vector_index_t(index.data(), index.size()));
        if (it == index_table_.end() || it->second.centroids.empty()) {
            return std::nullopt;
        }

        const auto &routing_table = it->second;
        centroid_id nearest_centroid = 0;
        float best_score = -1.0f; // cosine range: [-1, 1]

        for (const auto &centroid : routing_table.centroids) {
            float score = find_cosine_similarity(centroid.embedding, embedding);

            if (score > best_score) {
                best_score = score;
                nearest_centroid = centroid.id;
            }
        }

        const auto owner_it =
            routing_table.centroid_shard_mapping_.find(nearest_centroid);
        if (owner_it == routing_table.centroid_shard_mapping_.end()) {
            return std::nullopt;
        }

        return owner_it->second;
    }

    std::vector<shunyakv::CentroidScore>
    find_top_centroids(std::span<const float> query_embedding,
                       std::string_view index) {
        std::vector<shunyakv::CentroidScore> result;
        auto it =
            index_table_.find(shunyakv::vector_index_t(index.data(), index.size()));

        if (it == index_table_.end()) {
            return result;
        }

        const auto &routing_table = it->second;

        std::priority_queue<shunyakv::CentroidScore,
                            std::vector<shunyakv::CentroidScore>,
                            std::greater<shunyakv::CentroidScore>>
            winning_centroids;

        for (const auto &centroid : routing_table.centroids) {
            const auto owner_it =
                routing_table.centroid_shard_mapping_.find(centroid.id);
            if (owner_it == routing_table.centroid_shard_mapping_.end()) {
                continue;
            }

            float score =
                find_cosine_similarity(centroid.embedding, query_embedding);

            winning_centroids.push(shunyakv::CentroidScore{
                .score = score,
                .id = centroid.id,
                .target_shard_id = owner_it->second,
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
