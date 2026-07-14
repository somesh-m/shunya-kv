#pragma once

#include <vector>

#include "vector/memtable.hh"
#include "vector/search_result.hh"
#include "vector/vector_storage_manager.hh"
#include "vector/vector_types.hh"

namespace shunyakv {

class ManagedCentroidBucket {
  private:
    centroid_id id_;
    std::vector<float> embedding_;
    Memtable memtable_;

  public:
    ManagedCentroidBucket(centroid_id id, VectorStorageManager &storage)
        : id_(id), memtable_(storage) {}

    seastar::future<bool> insert(GenerationId id) {
        return memtable_.insert(id);
    }
    seastar::future<bool> erase(GenerationId id) {
        return memtable_.erase(id);
    }

    seastar::future<std::vector<VectorSearchResult>>
    search(const std::vector<float> &embedding, std::size_t top_k) const {
        return memtable_.search(embedding, top_k);
    }
};

} // namespace shunyakv
