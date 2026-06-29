#pragma once

#include <boost/intrusive/list_hook.hpp>
#include <cstdint>
#include <seastar/core/sstring.hh>
#include <string>
#include <utility>
#include <vector>
#include "vector/vector_types.hh"

class EntryPool;

namespace bi = boost::intrusive;

namespace shunyakv {
struct VectorEntry {
    std::string value;

    seastar::sstring key;

    uint32_t ver = 0;

    bool visited = false;

    std::vector<float> embedding;
    shunyakv::centroid_id centroid = 0;

    // Implement the eviction later once the service is separated

  public:
    bool is_in_use() const noexcept { return in_use_; }
    VectorEntry(seastar::sstring key, std::string value,
                std::vector<float> embedding, shunyakv::centroid_id centroid_id)
        : key(std::move(key)), value(std::move(value)),
          embedding(std::move(embedding)), centroid(centroid_id) {}

    bool in_use_ = false;
};
} // namespace shunyakv

namespace shunyakv::vdb {
using Entry = ::shunyakv::VectorEntry;
} // namespace shunyakv::vdb
