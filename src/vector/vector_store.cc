#include "vector/vector_store.hh"

#include <utility>

namespace shunyakv {

seastar::future<bool> VectorStore::vset(std::string_view index,
                                        std::string_view key,
                                        std::vector<float> embedding,
                                        std::string value,
                                        centroid_id centroid) {
    auto [it, inserted] = vector_index_map_.try_emplace(
        seastar::sstring(index),
        std::make_unique<VectorIndex>(
            VectorIndexConfig{.dim = static_cast<uint32_t>(embedding.size())}));

    it->second->upsert(seastar::sstring(key), std::move(embedding),
                       std::move(value), centroid);

    co_return true;
}

seastar::future<bool> VectorStore::vset_brute(std::string_view index,
                                              std::string_view key,
                                              std::vector<float> embedding,
                                              std::string value) {
    auto [it, inserted] = vector_index_map_.try_emplace(
        seastar::sstring(index),
        std::make_unique<VectorIndex>(
            VectorIndexConfig{.dim = static_cast<uint32_t>(embedding.size())}));

    it->second->upsert_brute(seastar::sstring(key), std::move(embedding),
                             std::move(value));

    co_return true;
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

} // namespace shunyakv
