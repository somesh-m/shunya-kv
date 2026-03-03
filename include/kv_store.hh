// kv_store.hh
#pragma once
#include "eviction/sieve_policy.hh"
#include "kv_types.hh"
#include "pool/object_pool.hh"
#include "ttl/entry.hh"
#include "ttl/heap_node.hh"
#include "ttl/ttl_cache.hh"
#include <absl/container/flat_hash_map.h>
#include <optional>
#include <seastar/core/future.hh>
#include <string_view>
/**
 * Each shard is supposed to have it's own copy of store
 */
using namespace seastar;
namespace shunyakv {
class store {

  public:
    future<> start(unsigned shard_id);
    future<> stop();

    /**
     * SET <key> <value>
     */
    future<bool> set(key_t key, sstring value);

    /**
     * GET <key> <value>
     * Don't make get const as it has to perform lazy deletion of expired keys
     */
    future<std::optional<sstring>> get(std::string_view key);

    /**
     * SET <key> <value> EX <ttl>
     */
    future<bool> set_with_ttl(key_t key, sstring value, uint64_t ttl);

  private:
    future<> check_memory_and_evict();

    /**
     * Use absl map to store key value pairs
     * EntryDeleter is used to return the GCd slots to the pool
     */
    absl::flat_hash_map < key_t, std::unique_ptr<ttl::Entry> _map;
    /**
     * Instance of object pool to allocate and release memory
     */
    CacheEntryPool entry_pool_{0};
    /**
     * Instance of sieve eviction policy
     */
    std::optional<SievePolicy> sieve_policy_;
    /**
     * Instance of ttl policy
     */
    ttl::TtlCache ttl_policy_;
    /**
     * Instance of a priority queue to maintain ttls
     */
    std::priority_queue<ttl::HeapNode, std::vector<ttl::HeapNode>,
                        ttl::MinExpiry>
        pq_;
};
} // namespace shunyakv
