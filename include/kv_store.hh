// kv_store.hh
#pragma once
#include <absl/container/flat_hash_map.h>
#include "kv_types.hh"
#include <seastar/core/future.hh>

namespace shunyakv {
class store {
  public:
    seastar::future<> start(unsigned shard_id);
    seastar::future<> stop();

    // SET k v
    seastar::future<bool> set(key_t key, seastar::sstring value);
    // GET k
    seastar::future<std::optional<seastar::sstring>>
    get(const key_t &key) const;
    // SET k v ex
    seastar::future<bool> set_with_ttl(key_t key, seastar::sstring value,
                                       uint64_t ttl);

  private:
    absl::flat_hash_map<key_t, seastar::sstring> _map;
};
} // namespace shunyakv
