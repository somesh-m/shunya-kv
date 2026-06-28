#include "vector/vector_store.hh"

#include <utility>

namespace shunyakv {

seastar::future<bool>
VectorStore::vset(std::string_view index, std::string_view key,
                  std::vector<float> embedding, std::string value,
                  centroid_id centroid_id) {
    if (!entry_pool_) {
        co_return false;
    }

    auto entry = co_await entry_pool_->acquire();
    if (!entry) {
        co_return false;
    }

    entry->key = seastar::sstring(key);
    entry->value = value;
    entry->embedding = embedding;
    entry->centroid_id = centroid_id;

    auto [it, inserted] = vector_index_map_.try_emplace(
        seastar::sstring(index),
        std::make_unique<VectorIndex>(
            VectorIndexConfig{.dim = static_cast<uint32_t>(embedding.size())}));

    it->second->upsert(seastar::sstring(key), std::move(embedding),
                       std::move(value), centroid_id);

    if (!inserted) {
        entry_pool_->release(std::move(entry));
    }

    co_return true;
}

seastar::future<std::vector<VectorSearchResult>>
VectorStore::vsearch(std::string_view index, std::vector<float> query_embedding,
                     uint32_t top_k, centroid_id id) {
    const auto it = vector_index_map_.find(index);

    if (it == vector_index_map_.end()) {
        co_return std::vector<VectorSearchResult>{};
    }

    co_return it->second->search(query_embedding, top_k, id);
}

seastar::future<> VectorStore::start(unsigned, const db_config &cfg,
                                     pool::ShardMemoryManager &memory_manager) {
    vector_index_map_.reserve(2700000);
    entry_pool_.emplace(memory_manager, cfg, 1000, 200);
    co_return;
}

seastar::future<> VectorStore::stop() {
    vector_index_map_.clear();
    vector_index_map_ = {};
    entry_pool_.reset();
    co_return;
}

} // namespace shunyakv
