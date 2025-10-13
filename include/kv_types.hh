// kv_types.hh
#pragma once

#include <seastar/core/sstring.hh>          // ← needed for seastar::sstring
#include <seastar/core/temporary_buffer.hh> // for temporary_buffer<char>

namespace shunyakv {

using key_t = seastar::sstring;

struct entry {
    mutable seastar::temporary_buffer<char> value;

    entry() = default;
    explicit entry(seastar::temporary_buffer<char> v) : value(std::move(v)) {}
};

} // namespace shunyakv
