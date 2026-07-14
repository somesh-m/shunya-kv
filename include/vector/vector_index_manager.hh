#pragma once

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "kv_types.hh"
#include "pool/vector_pool.hh"
#include "vector/centroid_bucket.hh"
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
#include "vector/vector_storage_manager.hh"

namespace shunyakv {
class VectorIndexManager {
  private:
    EntryPool &entry_pool_;
    VectorStorageManager storage_;
    absl::flat_hash_map<centroid_id, std::unique_ptr<ManagedCentroidBucket>>
        centroid_mapping_;

  public:
    explicit VectorIndexManager(EntryPool &entry_pool)
        : entry_pool_(entry_pool), storage_() {}

    // Disable copy/move to protect the internal references we pass down
    VectorIndexManager(const VectorIndexManager &) = delete;
    VectorIndexManager &operator=(const VectorIndexManager &) = delete;

    seastar::future<bool> insert(key_t key, std::vector<float> embedding,
                                 std::string value, centroid_id centroid_id) {
        if (storage_.exists(key)) {
            co_return false;
        }

        auto entry = co_await entry_pool_.acquire();
        if (storage_.exists(key)) {
            entry_pool_.release(std::move(entry));
            co_return false;
        }

        entry->key = std::move(key);
        entry->value = std::move(value);
        entry->embedding = std::move(embedding);
        entry->centroid = centroid_id;
        const auto id = storage_.insert(std::move(entry));
        if (!id.has_value()) {
            co_return false;
        }

        auto &bucket = centroid_mapping_[centroid_id];
        if (!bucket) {
            bucket =
                std::make_unique<ManagedCentroidBucket>(centroid_id, storage_);
        }

        if (!co_await bucket->insert(*id)) {
            auto rejected = storage_.erase(*id);
            if (rejected) {
                entry_pool_.release(std::move(rejected));
            }
            co_return false;
        }
        co_return true;
    }

    seastar::future<bool> erase(const key_t &key) {
        const auto id = storage_.generation_id(key);
        if (!id.has_value()) {
            co_return false;
        }

        const VectorEntry *entry = storage_.get(*id);
        if (entry == nullptr) {
            co_return false;
        }
        const centroid_id cid = entry->centroid;
        const auto bucket = centroid_mapping_.find(cid);
        if (bucket != centroid_mapping_.end()) {
            co_await bucket->second->erase(*id);
        }

        auto removed = storage_.erase(key);
        if (!removed) {
            co_return false;
        }
        entry_pool_.release(std::move(removed));
        co_return true;
    }

    seastar::future<std::vector<VectorSearchResult>>
    search(const std::vector<float> &embedding, centroid_id centroid,
           std::size_t top_k) const {
        const auto bucket = centroid_mapping_.find(centroid);
        if (bucket == centroid_mapping_.end()) {
            co_return std::vector<VectorSearchResult>{};
        }
        co_return co_await bucket->second->search(embedding, top_k);
    }

    std::optional<seastar::sstring> get(const key_t &key) const {
        const auto id = storage_.generation_id(key);
        if (!id.has_value()) {
            return std::nullopt;
        }
        const VectorEntry *entry = storage_.get(*id);
        if (entry == nullptr) {
            return std::nullopt;
        }
        return seastar::sstring(entry->value);
    }

    const VectorStorageManager &get_storage() const { return storage_; }
};
} // namespace shunyakv
