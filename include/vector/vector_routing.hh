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
using shard_id = uint32_t;

class VDBOrchestrator {
    VDBOrchestrator() {}

  public:
    void add_centroid(std::vector<float> embedding, std::string_view index) {
        centroid_id id = routing_table_.centroids.size() + 1;
        auto cent = Centroid{id : id, embedding : std::move(embedding)};
        shard_id owner_id = choose_owner(cent.id);
        routing_table_.centroid_shard_mapping_.insert(cent.id, owner_id);
        routing_table_.centroids.push_back(std::move(cent));
    }

    std::optional<shard_id>
    find_vector_owner_shard(std::span<const float> embedding,
                            std::string_view index) {
        if (routing_table_.centroids.empty()) {
            return std::nullopt;
        }

        centroid_id nearest_centroid = 0;
        float best_score = -1.0f; // cosine range: [-1, 1]

        for (const auto &centroid : routing_table_.centroids) {
            float score = find_cosine_similarity(centroid.embedding, embedding);

            if (score > best_score) {
                best_score = score;
                nearest_centroid = centroid.id;
            }
        }

        auto it = routing_table_.centroid_shard_mapping.find(nearest_centroid);
        if (it == routing_table_.centroid_shard_mapping.end()) {
            return std::nullopt;
        }

        return it->second;
    }

    std::vector<CentroidScore>
    find_top_centroids(std::span<const float> query_embedding,
                       std::string_view index) {
        std::vector<CentroidScore> result = {};
        if (routing_table_.centroids.empty()) {
            return result;
        }

        auto it = index_table_.find(index);

        if (it == index_table_.end()) {
            return std::nullopt;
        }

        auto &routing_table = it->second;

        std::priority_queue<CentroidScore, std::vector<CentroidScore>,
                            std::greater<CentroidScore>>
            winning_centroids;

        for (uint32_t i = 0; i < routing_table.centroids.size(); i++) {
            const auto &centroid = routing_table.centroids[i];

            float score =
                find_cosine_similarity(centroid.embedding, query_embedding);

            winning_centroids.push(CentroidScore{
                .score = score,
                .id = centroid.id,
                .target_shard_id =
                    routing_table.centroid_shard_mapping_.find(item.id)});

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

    size_t nprobe() { return nprobe_; }

  private:
    std::unordered_map<vector_index_t, CentroidTable> index_table_;
    uint32_t current_id;
    size_t nprobe_ = 5;

    shard_id choose_owner(vdb::centroid_id id) {
        return id % seastar::smp::count;
    }
};
} // namespace vdb
