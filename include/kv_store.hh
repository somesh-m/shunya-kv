// kv_store.hh
#pragma once
#include "dbconfig.hh"
#include "eviction/sieve_policy.hh"
#include "kv_types.hh"
#include "pool/object_pool.hh"
#include "shard_stats.hh"
#include "ttl/entry.hh"
#include "ttl/heap_node.hh"
#include <absl/container/flat_hash_map.h>
#include <optional>
#include <queue>
#include <seastar/core/future.hh>
#include <string_view>

/**
 * Each shard is supposed to have it's own copy of store
 */
using namespace seastar;
namespace shunyakv {
class store {

  public:
    future<> start(unsigned shard_id, const db_config &cfg);
    future<> stop();

    /**
     * SET <key> <value>
     */
    future<bool> set(std::string_view key, sstring value);

    /**
     * GET <key> <value>
     * Don't make get const as it has to perform lazy deletion of expired keys
     */
    future<std::optional<seastar::sstring>> get(std::string_view key);

    /**
     * SET <key> <value> EX <ttl>
     */
    future<bool> set_with_ttl(std::string_view key, sstring value,
                              uint64_t ttl);
    shard_stats_snapshot snapshot_stats() const noexcept;
    void record_cache_miss_count(std::size_t n = 1) noexcept {
        stats_.record_cache_miss_count(n);
    }

    /**
     * Perform TTL eviction
     */
    future<> evict_ttl_keys(uint64_t now, std::size_t budget);
    inline bool is_expired(uint64_t now, uint64_t expires_at) {
        return now >= expires_at;
    }

    void set_usable_memory(double reseve_percentage);

  private:
    future<> check_memory_and_evict();

    /**
     * Use absl map to store key value pairs
     * EntryDeleter is used to return the GCd slots to the pool
     */
    absl::flat_hash_map<key_t, std::unique_ptr<ttl::Entry>> _map;
    /**
     * Instance of object pool to allocate and release memory
     */
    CacheEntryPool entry_pool_{0};
    /**
     * Instance of sieve eviction policy
     */
    eviction::EvictionConfig ev_cfg_{};
    std::size_t usable_memory_;
    std::optional<SievePolicy> sieve_policy_;
    shard_stats stats_;
    /**
     * Instance of a priority queue to maintain ttls
     */
    std::priority_queue<ttl::HeapNode, std::vector<ttl::HeapNode>,
                        ttl::MinExpiry>
        pq_;
};
} // namespace shunyakv
