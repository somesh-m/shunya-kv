#pragma once

#include "eviction/sieve_policy.hh"
#include "pool/object_pool.hh"
#include "ttl/entry.hh"
#include "ttl/heap_node.hh"
#include "ttl/ttl_policy.hh"

#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <seastar/core/future.hh>
#include <string>
#include <unordered_map>

namespace ttl {
class TtlCache {
  public:
    TtlCache() {}

    // Evict expired entries. 'budget' prevents long stalls
    seastar::future<std::vector<ttl::Entry &>>
    evict(uint64_t now, std::size_t budget = 10000);

    std::size_t size() const { return kv_.size(); }

    // Optional: only extend TTL if remaining time < threshold (reduces heap
    // churn)
    void set_extend_threshold(uint64_t t) { extend_threshold_ = t; };

  private:
    static bool is_expired(uint64_t now, uint64_t expires_at) {
        return now >= expires_at;
    }

    std::unordered_map<std::string, std::unique_ptr<Entry>> kv_;
    std::priority_queue<HeapNode, std::vector<HeapNode>, MinExpiry> pq_;

    // If 0 => always extend when policy_ is set
    uint64_t extend_threshold_ = 0;
};
} // namespace ttl
