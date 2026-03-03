#pragma once

#include <cstdint>
#include <string>

namespace ttl {
struct HeapNode {
    uint64_t expires_at = 0;
    std::string_view key;
    uint32_t ver = 0;
};

struct MinExpiry {
    bool operator()(const HeapNode &a, const HeapNode &b) const {
        return a.expires_at > b.expires_at;
    }
};
} // namespace ttl
