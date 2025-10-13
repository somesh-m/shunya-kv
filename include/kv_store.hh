// kv_store.hh
#pragma once
#include "kv_types.hh"
#include <seastar/core/future.hh>
#include <unordered_map>

namespace shunyakv {
class store {
  public:
    seastar::future<> start(unsigned shard_id);
    seastar::future<> stop();

    // SET k v
    seastar::future<bool> set(key_t key, std::string value);
    // GET k
    seastar::future<std::optional<std::string>>
    get(const key_t &key) const; // std::string out

  private:
    std::unordered_map<key_t, seastar::sstring> _map; // internal storage
};
}; // namespace shunyakv
