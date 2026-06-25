#pragma once

#include <boost/intrusive/list_hook.hpp>
#include <cstdint>
#include <seastar/core/sstring.hh>
#include <string>
#include <utility>
#include <vector>
#include <vector_types.hh>

class EntryPool;

namespace bi = boost::intrusive;

namespace shunyakv::vdb {
struct Entry {
    std::string value;

    seastar::sstring key;

    uint32_t ver = 0;

    bool visited = false;

    std::vector<float> embedding;
    centroid_id centroid_id = 0;

    // Implement the eviction later once the service is separated

  public:
    bool is_in_use() const noexcept { return in_use_; }
    Entry(seastar::sstring key, std::string value, std::vector<float> embedding,
          centroid_id centroid_id)
        : key(std::move(key)), value(std::move(value)),
          embedding(std::move(embedding)), centroid_id(centroid_id) {}

  private:
    bool in_use_ = false;

    friend class EntryPool;
};
} // namespace shunyakv::vdb
