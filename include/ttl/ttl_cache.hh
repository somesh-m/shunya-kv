#pragma once

#include "ttl/entry.hh"
#include "ttl/heap_node.hh"
#include "ttl/ttl_policy.hh"

#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>

namespace ttl {
class TtlCache {
  public:
    explicit TtlCache(const ttl_policy *policy = nullptr);

    void set(const std::string &key, std::string value, uint64_t now,
             uint64_t ttl);
    std::optional<std::string> get(const std::string &key, uint64_t now);

    // Evict expired entries. 'budget' prevents long stalls
    std::size_t evict(uint64_t now, std::size_t budget = 10000);

    std::size_t size() const { return kv_.size(); }

    bool del(const std::string &key);

    // Optional: only extend TTL if remaining time < threshold (reduces heap
    // churn)
    void set_extend_threshold(uint64_t t) { extend_threshold_ = t; };

  private:
    static bool is_expired(uint64_t now, uint64_t expires_at) {
        return now >= expires_at;
    }

    std::unordered_map<std::string, Entry> kv_;
    std::priority_queue<HeapNode, std::vector<HeapNode>, MinExpiry> pq_;

    const ttl_policy *policy_ = nullptr;

    // If 0 => always extend when policy_ is set
    uint64_t extend_threshold_ = 0;
};
} // namespace ttl
