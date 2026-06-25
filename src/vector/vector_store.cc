#include "vector/vector_store.hh"
#include "kv_types.hh"
#include "pool/shard_memory_manager.hh"

using Clock = std::chrono::steady_clock;
namespace shunykv {

seastar::future<bool> vset(std::string_view index, std::string_view key,
                           std::vector<float> embedding, std::string value) {
    auto entry = co_await entry_pool_->acquire();
    if (!entry) {
        // TODO: Handle error logging here for memory full
        co_return false;
    }

    entry->key = seastar::sstring(key);
    entry->value = std::move(value);

    auto [it, inserted] = vector_index_map_.try_emplace(
        seastar::sstring(index),
        std::make_unique<VectorIndex>({dim : embedding.size()}));
    if (!inserted) {
        // Index already exists
        it->second->upsert(seastar::sstring(key), embedding, std::move(value));
    }
}

seastar::future<std::optional<seastar::sstring>>
vsearch(std::string_view index, std::vector<float> embedding) {}

// Lifecycle methods
seastar::future<> store::start(unsigned, const db_config &cfg,
                               pool::ShardMemoryManager &memory_manager) {
    vector_index_map_.reserve(27000'00);
    entry_pool_.emplace(memory_manager, cfg, 1000, 200);
    // Initialize the eviction algorithm here later
    co_return;
}

seastar::future<> store::stop() {
    vector_index_map_.clear();
    vector_index_map_ = {};
    co_return;
}

} // namespace shunykv
