#pragma once

#include <absl/container/flat_hash_map.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "kv_types.hh"
#include "vector/helper.hh"
#include "vector/search_result.hh"
#include "vector/vector_entry.hh"
#include "vector/vector_types.hh"
#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <random>
#include <seastar/coroutine/maybe_yield.hh>
#include <stdexcept>

namespace shunyakv {
class VectorStorageManager {
  private:
    std::vector<std::unique_ptr<shunyakv::VectorEntry>> entries_;
    absl::flat_hash_map<key_t, GenerationId> key_to_id_;
    std::vector<GenerationId> free_stack_; // Consider using absl inlined vector
                                           // to improve performance
    std::vector<uint64_t> slot_generations_;

  public:
    VectorStorageManager() = default;

    // Disallow copies to ensure single-owner safety
    VectorStorageManager(const VectorStorageManager &) = delete;
    VectorStorageManager &operator=(const VectorStorageManager &) = delete;

    bool exists(const key_t &key) const {
        return key_to_id_.find(key) != key_to_id_.end();
    }

    std::size_t total_count() const { return key_to_id_.size(); }

    std::optional<GenerationId>
    insert(std::unique_ptr<shunyakv::VectorEntry> entry) {
        if (!free_stack_.empty()) {
            GenerationId id = free_stack_.back();
            ++id.generation;

            const bool inserted = key_to_id_.try_emplace(entry->key, id).second;
            if (!inserted) {
                return std::nullopt;
            }

            free_stack_.pop_back();
            slot_generations_[id.index] = id.generation;
            entries_[id.index] = std::move(entry);
            return id;
        }

        const auto index = entries_.size();
        if (index == entries_.capacity()) {
            entries_.reserve(index == 0 ? 1 : index * 2);
        }
        if (index == slot_generations_.capacity()) {
            slot_generations_.reserve(index == 0 ? 1 : index * 2);
        }

        const bool inserted =
            key_to_id_.try_emplace(entry->key, GenerationId{index, 0}).second;
        if (!inserted) {
            return std::nullopt;
        }

        entries_.push_back(std::move(entry));
        slot_generations_.push_back(0);
        return GenerationId{index, 0};
    }

    VectorEntry *get(GenerationId id) noexcept {
        if (id.index >= entries_.size() ||
            slot_generations_[id.index] != id.generation) {
            return nullptr;
        }
        return entries_[id.index].get();
    }

    const VectorEntry *get(GenerationId id) const noexcept {
        if (id.index >= entries_.size() ||
            slot_generations_[id.index] != id.generation) {
            return nullptr;
        }
        return entries_[id.index].get();
    }

    std::optional<GenerationId> generation_id(const key_t &key) const {
        const auto it = key_to_id_.find(key);
        if (it == key_to_id_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::unique_ptr<VectorEntry> erase(const key_t &key) {
        const auto it = key_to_id_.find(key);
        if (it == key_to_id_.end()) {
            return nullptr;
        }

        const GenerationId id = it->second;
        key_to_id_.erase(it);
        auto entry = std::move(entries_[id.index]);
        free_stack_.push_back(id);
        return entry;
    }

    std::unique_ptr<VectorEntry> erase(GenerationId id) {
        VectorEntry *entry = get(id);
        if (entry == nullptr) {
            return nullptr;
        }
        return erase(entry->key);
    }
};
} // namespace shunyakv
