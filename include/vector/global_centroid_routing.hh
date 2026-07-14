#pragma once

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "vector/helper.hh"
#include "vector/vector_types.hh"

namespace shunyakv {

class GlobalCentroidRouting {
  private:
    uint32_t dimensions_ = 0;
    uint32_t num_centroids_ = 0;
    std::vector<float> centroids_;
    std::vector<seastar::shard_id> centroid_to_shard_;

    // VGET/VDELETE do not carry an embedding, so retain the owner selected by
    // VSET. This directory is replicated by service on every shard.
    absl::flat_hash_map<key_t, VectorPoint> key_owners_;

    void clear_table() {
        dimensions_ = 0;
        num_centroids_ = 0;
        centroids_.clear();
        centroid_to_shard_.clear();
    }

  public:
    GlobalCentroidRouting() = default;
    GlobalCentroidRouting(const GlobalCentroidRouting &) = delete;
    GlobalCentroidRouting &operator=(const GlobalCentroidRouting &) = delete;
    GlobalCentroidRouting(GlobalCentroidRouting &&) noexcept = default;
    GlobalCentroidRouting &operator=(GlobalCentroidRouting &&) noexcept =
        default;

    bool load_from_file(const std::string &filepath) {
        clear_table();
        key_owners_.clear();
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        file.read(reinterpret_cast<char *>(&dimensions_), sizeof(dimensions_));
        file.read(reinterpret_cast<char *>(&num_centroids_),
                  sizeof(num_centroids_));
        if (!file || dimensions_ == 0 || num_centroids_ == 0) {
            clear_table();
            return false;
        }

        const std::size_t centroid_count = num_centroids_;
        if (centroid_count >
            std::numeric_limits<std::size_t>::max() / dimensions_) {
            clear_table();
            return false;
        }
        const std::size_t total_elements = centroid_count * dimensions_;
        centroids_.resize(total_elements);
        file.read(reinterpret_cast<char *>(centroids_.data()),
                  static_cast<std::streamsize>(total_elements * sizeof(float)));
        if (!file) {
            clear_table();
            return false;
        }

        for (uint32_t id = 0; id < num_centroids_; ++id) {
            ::vdb::normalize(std::span<float>(
                centroids_.data() + static_cast<std::size_t>(id) * dimensions_,
                dimensions_));
        }

        centroid_to_shard_.resize(num_centroids_);
        for (uint32_t id = 0; id < num_centroids_; ++id) {
            uint32_t shard = 0;
            file.read(reinterpret_cast<char *>(&shard), sizeof(shard));
            if (!file) {
                clear_table();
                return false;
            }
            centroid_to_shard_[id] = seastar::shard_id{shard};
        }
        return true;
    }

    std::vector<VectorPoint>
    route_embedding(std::span<const float> query_embedding,
                    std::size_t nprobe) const {
        if (query_embedding.size() != dimensions_ || centroids_.empty() ||
            nprobe == 0) {
            return {};
        }

        struct ScoredCentroid {
            float score;
            centroid_id id;
        };

        std::vector<ScoredCentroid> scored;
        scored.reserve(num_centroids_);
        for (centroid_id id = 0; id < num_centroids_; ++id) {
            const std::span<const float> centroid(
                centroids_.data() + static_cast<std::size_t>(id) * dimensions_,
                dimensions_);
            const float score = static_cast<float>(
                ::vdb::find_cosine_similarity(query_embedding, centroid));
            scored.push_back(ScoredCentroid{.score = score, .id = id});
        }

        const std::size_t probe_count = std::min(nprobe, scored.size());
        std::partial_sort(
            scored.begin(), scored.begin() + probe_count, scored.end(),
            [](const ScoredCentroid &a, const ScoredCentroid &b) {
                if (a.score != b.score) {
                    return a.score > b.score;
                }
                return a.id < b.id;
            });

        std::vector<VectorPoint> routes;
        routes.reserve(probe_count);
        for (std::size_t i = 0; i < probe_count; ++i) {
            const centroid_id id = scored[i].id;
            routes.push_back(
                VectorPoint{.id = id,
                            .target_shard_id = centroid_to_shard_[id]});
        }
        return routes;
    }

    std::vector<VectorPoint> route_embedding(const float *query_embedding,
                                             std::size_t nprobe) const {
        if (query_embedding == nullptr) {
            return {};
        }
        return route_embedding(
            std::span<const float>(query_embedding, dimensions_), nprobe);
    }

    void remember_owner(key_t key, VectorPoint owner) {
        key_owners_.insert_or_assign(std::move(key), owner);
    }

    void forget_owner(const key_t &key) { key_owners_.erase(key); }

    std::optional<VectorPoint> owner_for(const key_t &key) const {
        const auto it = key_owners_.find(key);
        if (it == key_owners_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    uint32_t dimensions() const noexcept { return dimensions_; }
    uint32_t num_centroids() const noexcept { return num_centroids_; }
    bool loaded() const noexcept { return !centroids_.empty(); }
};

using GlobalRouter = GlobalCentroidRouting;

} // namespace shunyakv
