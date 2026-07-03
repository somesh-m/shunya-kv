#include "vector/vector_store.hh"

#include <utility>
#include <seastar/coroutine/maybe_yield.hh>

namespace shunyakv {

seastar::future<bool> VectorStore::vset(std::string_view index,
                                        std::string_view key,
                                        std::vector<float> embedding,
                                        std::string value,
                                        centroid_id centroid) {
    auto [it, inserted] = vector_index_map_.try_emplace(
        seastar::sstring(index),
        std::make_unique<VectorIndex>(
            VectorIndexConfig{.dim = static_cast<uint32_t>(embedding.size())},
            seastar::sstring(index)));

    it->second->upsert(seastar::sstring(key), std::move(embedding),
                       std::move(value), centroid);
    ++write_generation_;

    co_return true;
}

seastar::future<bool> VectorStore::vset_brute(std::string_view index,
                                              std::string_view key,
                                              std::vector<float> embedding,
                                              std::string value) {
    auto [it, inserted] = vector_index_map_.try_emplace(
        seastar::sstring(index),
        std::make_unique<VectorIndex>(
            VectorIndexConfig{.dim = static_cast<uint32_t>(embedding.size())},
            seastar::sstring(index)));

    it->second->upsert_brute(seastar::sstring(key), std::move(embedding),
                             std::move(value));
    ++write_generation_;

    co_return true;
}

seastar::future<std::optional<sstring>>
VectorStore::vget(std::string_view index, std::string_view key) {
    const auto index_it = vector_index_map_.find(seastar::sstring(index));

    if (index_it == vector_index_map_.end()) {
        co_return std::nullopt;
    }

    const auto *entry = index_it->second->get(seastar::sstring(key));
    if (entry == nullptr) {
        co_return std::nullopt;
    }

    co_return sstring(entry->value);
}

seastar::future<std::vector<VectorSearchResult>>
VectorStore::vsearch(std::string_view index, std::vector<float> query_embedding,
                     uint32_t top_k, centroid_id id) {
    const auto it = vector_index_map_.find(seastar::sstring(index));

    if (it == vector_index_map_.end()) {
        co_return std::vector<VectorSearchResult>{};
    }

    co_return it->second->search(query_embedding, top_k, id);
}

seastar::future<std::vector<VectorSearchResult>>
VectorStore::vsearch_brute(std::string_view index,
                           std::vector<float> query_embedding, uint32_t top_k) {
    const auto it = vector_index_map_.find(seastar::sstring(index));

    if (it == vector_index_map_.end()) {
        co_return std::vector<VectorSearchResult>{};
    }

    co_return it->second->search_brute(query_embedding, top_k);
}

seastar::future<> VectorStore::start(unsigned, const db_config &cfg,
                                     pool::ShardMemoryManager &) {
    vector_index_map_.reserve(2700000);
    co_return;
}

seastar::future<> VectorStore::stop() {
    vector_index_map_.clear();
    vector_index_map_ = {};
    co_return;
}

// Indexing related functions
seastar::future<std::vector<LocalCentroidSnapshot>>
VectorStore::build_local_index() {
    // This needs to be done per index
    index_building_ = true;
    co_await seastar::coroutine::maybe_yield();
    std::vector<LocalCentroidSnapshot> snapshots;
    snapshots.reserve(vector_index_map_.size());
    for (const auto &[key, entry] : vector_index_map_) {
        auto snapshot = co_await entry->build_local_index(centroid_group_count_);
        if (snapshot) {
            snapshots.push_back(std::move(*snapshot));
        }
    }

    /** Increment the centroid group count.
     * Change to increase or decrease based on the number of keys increased or
     * removed
     */
    centroid_group_count_ *= 5;
    index_enabled_ = true;
    index_building_ = false;
    index_version_ += 1;
    co_return snapshots;
}

std::size_t VectorStore::local_entry_count() const {
    std::size_t local_count = 0;
    for (const auto &[index, entry] : vector_index_map_) {
        local_count += entry->total_entry_count();
    }
    return local_count;
}

VectorStoreInfoSnapshot VectorStore::snapshot_info() const {
    VectorStoreInfoSnapshot snapshot;
    snapshot.local_entry_count = local_entry_count();
    snapshot.write_generation = write_generation_;
    snapshot.index_enabled = index_enabled_;
    snapshot.index_building = index_building_;
    snapshot.local_indexes.reserve(vector_index_map_.size());

    for (const auto &[index, _] : vector_index_map_) {
        snapshot.local_indexes.push_back(index);
    }

    return snapshot;
}

} // namespace shunyakv
