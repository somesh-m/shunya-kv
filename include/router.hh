#pragma once
#include "kv_store.hh"

#include <functional> // std::hash
#include <optional>
#include <seastar/core/future.hh>
#include <seastar/core/smp.hh>              // smp::count, this_shard_id
#include <seastar/core/temporary_buffer.hh> // seastar::temporary_buffer
#include <seastar/net/api.hh>
#include <string_view> // not <string_value>

// static seastar::logger store_logger{"store"};
namespace shunyakv {

// inline unsigned shard_for(std::string_view key) {
//     return static_cast<unsigned>(std::hash<std::string_view>{}(key) %
//                                  seastar::smp::count);
// }

// TODO: Move all the logic to router.cc, we want to keep the header file clean
class service {
  public:
    seastar::future<> start() {
        // store_logger.info("Starting store on shard {}",
        //                   seastar::this_shard_id());
        return _store.start(seastar::this_shard_id());
    }

    seastar::future<> stop() { return _store.stop(); }

    // Cross-shard safe API (std::string over the wire)
    seastar::future<bool> local_set(std::string key, std::string value) {
        key_t k{key.data(), key.size()}; // sstring on this shard
        return _store.set(std::move(k), std::move(value));
    }
    seastar::future<bool> local_set(std::string key, std::string value,
                                    uint64_t ttl) {
        key_t k{key.data(), key.size()}; // sstring on this shard
        return _store.set_with_ttl(std::move(k), std::move(value), ttl);
    }
    seastar::future<std::optional<std::string>>
    local_get(const std::string &key) const {
        key_t k{key.data(), key.size()};
        return _store.get(k);
    }

  private:
    store _store;
};

} // namespace shunyakv
