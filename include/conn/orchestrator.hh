#pragma once

#include <functional>
#include <memory>
#include <utility>

#include <seastar/core/future.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/net/api.hh> // socket_address, listen_options
#include <seastar/net/tls.hh> // tls::server_credentials

#include "socket_handler.hh"
#include "socket_server.hh"

using namespace seastar;
namespace shunyakv {
class socket_server_orchestrator {
    std::unique_ptr<sharded<socket_server>> _server_dist;

  public:
    socket_server_orchestrator() : _server_dist(new sharded<socket_server>) {}
    using handler_factory_t = std::function<std::unique_ptr<SocketHandler>()>;
    future<> start();
    future<> stop() noexcept;
    future<> set_handler(handler_factory_t handler);
    future<> listen(socket_address addr);
    future<>
    listen(socket_address addr,
           seastar::shared_ptr<seastar::tls::server_credentials> credentials);
    future<> listen(socket_address addr, listen_options lo);
    future<>
    listen(socket_address addr, listen_options lo,
           seastar::shared_ptr<seastar::tls::server_credentials> credentials);
    sharded<socket_server> &server();
};
} // namespace shunyakv
