#pragma once
#include "vector/helper.hh"
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

struct Centroid {
    centroid_id id;
    std::vector<float> embedding;
};

struct CentroidScore {
    float score;
    centroid_id id;

    bool operator>(const CentroidScore &other) const {
        return score > other.score;
    }
};

struct CentroidTable {
    std::vector<Centroid> centroids;
    std::unordered_map<centroid_id, shard_id> centroid_shard_mapping_;
    uint64_t version = 0;
};

struct QueryScatterResult {
    centroid_id id;
    shard_id target_shard_id;
};

class VDBOrchestrator {
    VDBOrchestrator() {}

  public:
    void add_centroid(std::vector<float> embedding) {
        centroid_id id = routing_table_.centroids.size() + 1;
        auto cent = Centroid{id : id, embedding : std::move(embedding)};
        shard_id owner_id = choose_owner(cent.id);
        routing_table_.centroid_shard_mapping_.insert(cent.id, owner_id);
        routing_table_.centroids.push_back(std::move(cent));
    }

    std::optional<shard_id>
    get_destination_shard(std::span<const float> embedding) {
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

    std::optional<std::vector<QueryScatterResult>>
    get_query_shard(std::span<const float> query_embedding) {
        if (routing_table_.centroids.empty()) {
            return std::nullopt;
        }

        std::priority_queue<CentroidScore, std::vector<CentroidScore>,
                            std::greater<CentroidScore>>
            winning_centroids;

        for (uint32_t i = 0; i < routing_table_.centroids.size(); i++) {
            const auto &centroid = routing_table_.centroids[i];

            float score =
                find_cosine_similarity(centroid.embedding, query_embedding);

            winning_centroids.push(
                CentroidScore{.score = score, .id = centroid.id});

            if (winning_centroids.size() > nprobe_) {
                winning_centroids.pop();
            }
        }

        std::vector<QueryScatterResult> result;
        result.reserve(winning_centroids.size());

        while (!winning_centroids.empty()) {
            auto item = winning_centroids.top();
            winning_centroids.pop();

            auto shard_it =
                routing_table_.centroid_shard_mapping_.find(item.id);

            if (shard_it == routing_table_.centroid_shard_.mapping_.end()) {
                continue;
            }

            result.push_back(QueryScatterResult{
                .id = item.id, .target_shard_id = shard_it->second});
        }

        return result;
    }

  private:
    CentroidTable routing_table_;
    uint32_t current_id;
    uint32_t nprobe_ = 5;

    shard_id choose_owner(centroid_id id) { return id % seastar::smp::count; }
};
} // namespace vdb
