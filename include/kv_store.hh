// kv_store.hh
#pragma once
#include "kv_types.hh"
#include <absl/container/flat_hash_map.h>
#include <seastar/core/future.hh>
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
     */
    future<std::optional<sstring>> get(const key_t &key) const;

    /**
     * SET <key> <value> EX <ttl>
     */
    future<bool> set_with_ttl(key_t key, sstring value, uint64_t ttl);

  private:
    absl::flat_hash_map<key_t, seastar::sstring> _map;
};
} // namespace shunyakv
