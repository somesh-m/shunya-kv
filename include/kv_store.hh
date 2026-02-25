// kv_store.hh
#pragma once
#include "kv_types.hh"
#include "ttl/entry.hh"
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
     */
    future<std::optional<sstring>> get(std::string_view key) const;

    /**
     * SET <key> <value> EX <ttl>
     */
    future<bool> set_with_ttl(key_t key, sstring value, uint64_t ttl);

  private:
    absl::flat_hash_map<key_t, ttl::Entry> _map;
};
} // namespace shunyakv
