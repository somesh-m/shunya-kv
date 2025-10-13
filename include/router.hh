#pragma once
#include "kv_store.hh"

#include <functional> // std::hash
#include <optional>
#include <seastar/core/future.hh>
#include <seastar/core/smp.hh>              // smp::count, this_shard_id
#include <seastar/core/temporary_buffer.hh> // seastar::temporary_buffer
#include <seastar/net/api.hh>
#include <string_view> // not <string_value>

namespace shunyakv {

// inline unsigned shard_for(std::string_view key) {
//     return static_cast<unsigned>(std::hash<std::string_view>{}(key) %
//                                  seastar::smp::count);
// }

class service {
  public:
    seastar::future<> start() {
        std::cout << "Starting store on shard " << seastar::this_shard_id()
                  << "\n";
        return _store.start(seastar::this_shard_id());
    }

    seastar::future<> stop() { return _store.stop(); }

    void handle_session(seastar::connected_socket s,
                        seastar::socket_address peer, uint16_t port) {
        std::cout << "accepted data on shard " << seastar::this_shard_id()
                  << "\n";
    }

    // Cross-shard safe API (std::string over the wire)
    seastar::future<bool> local_set(std::string key, std::string value) {
        key_t k{key.data(), key.size()}; // sstring on this shard
        return _store.set(std::move(k), std::move(value));
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
