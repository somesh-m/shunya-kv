#pragma once

#include <cstdint>
#include <string>

namespace ttl {
struct HeapNode {
    std::string_view key;
    uint64_t expires_at = 0;
    uint32_t ver = 0;
};

struct MinExpiry {
    bool operator()(const HeapNode &a, const HeapNode &b) const {
        return a.expires_at > b.expires_at;
    }
};
} // namespace ttl
