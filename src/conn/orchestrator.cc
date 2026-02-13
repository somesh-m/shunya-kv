#include <functional>

#include <seastar/core/future.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/net/api.hh>
#include <seastar/net/tls.hh>
#include <seastar/util/log.hh>

#include "conn/orchestrator.hh"
#include "conn/socket_server.hh"

using namespace seastar;
namespace shunyakv {
static seastar::logger orch_logger("orchestrator");

future<> socket_server_orchestrator::start() { return _server_dist->start(); }
future<> socket_server_orchestrator::stop() noexcept {
    return _server_dist->stop();
}
future<> socket_server_orchestrator::set_handler(handler_factory_t handler) {
    return _server_dist->invoke_on_all(
        [handler = std::move(handler)](socket_server &server) mutable {
            server.set_handler(std::move(handler));
        });
}
future<> socket_server_orchestrator::listen(socket_address addr) {
    return _server_dist
        ->invoke_on_all<future<> (socket_server::*)(socket_address)>(
            &socket_server::listen, addr);
}
future<> socket_server_orchestrator::listen(
    socket_address addr,
    seastar::shared_ptr<tls::server_credentials> credentials) {
    return _server_dist->invoke_on_all<future<> (socket_server::*)(
        socket_address, seastar::shared_ptr<seastar::tls::server_credentials>)>(
        &socket_server::listen, addr, credentials);
}

future<> socket_server_orchestrator::listen(socket_address addr,
                                            listen_options lo) {
    return _server_dist->invoke_on_all<future<> (socket_server::*)(
        socket_address, listen_options)>(&socket_server::listen, addr, lo);
}

// future<> socket_server_orchestrator::listen(
//     socket_address addr, listen_options lo,
//     seastar::shared_ptr<tls::server_credentials> credentials) {
//     return _server_dist->invoke_on_all <
//            future<>(socket_server::*)(
//                socket_address,
//                listen_options,
//                seastar::shared_ptr<seastar::tls::server_credentials>)(
//                &socket_server::listen,
//                addr,
//                lo,
//                credentials);
// }

} // namespace shunyakv
