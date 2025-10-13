// server.hh
#pragma once

#include <cstdint>
#include <seastar/core/abort_source.hh> // for _as
#include <seastar/core/distributed.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh> // <-- add this
#include <seastar/net/api.hh>

namespace shunyakv {

// Forward-declare your per-shard service (defined in service.hh / service.cc)
class service;

class tcp_server {
  public:
    // Construct a server bound to `port`, operating against the provided shard
    // set.
    explicit tcp_server(std::uint16_t port, seastar::sharded<service> &store)
        : _port(port), _store(store) {}

    // Start accepting connections (idempotent).
    seastar::future<> start();

    // Stop accepting; existing sessions wind down. Idempotent.
    seastar::future<> stop();

    // Non-copyable / non-movable
    tcp_server(const tcp_server &) = delete;
    tcp_server &operator=(const tcp_server &) = delete;
    tcp_server(tcp_server &&) = delete;
    tcp_server &operator=(tcp_server &&) = delete;

  private:
    // Per-connection coroutine: runs the line protocol for one client.
    seastar::future<> handle_session(seastar::connected_socket s,
                                     seastar::socket_address peer,
                                     service &store);

  private:
    std::uint16_t _port{0};
    bool _running{false};
    seastar::server_socket _listener;
    seastar::sharded<service> &_store;
    seastar::gate _gate;
    seastar::abort_source _as;
    std::optional<seastar::future<>> _accept_loop;
};

} // namespace shunyakv
