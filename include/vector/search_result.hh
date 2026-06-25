#pragma once

#include <cstdint>
#include <seastar/core/sstring.hh>
#include <string>
#include <vector_types.hh>

namespace shunyakv {
struct VectorSearchResult {
    key_t key;
    float score = 0.0f;
    seastar::temporary_buffer<char> value;
};
} // namespace shunyakv
