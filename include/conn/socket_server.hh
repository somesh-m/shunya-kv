#pragma once

#include <functional>
#include <memory>

#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/core/sstring.hh>
#include <seastar/net/api.hh>
#include <seastar/net/tls.hh>

#include "connections.hh"
#include "socket_handler.hh"
#include <boost/intrusive/list.hpp>
#include <limits>
#include <optional>
#include <vector>

namespace shunyakv {
class socket_server {
  public:
    using handler_factory_t = std::function<std::unique_ptr<SocketHandler>()>;

  private:
    std::vector<seastar::server_socket> _listeners;
    handler_factory_t _handler_factory;

    uint64_t _total_connections = 0;
    uint64_t _current_connections = 0;
    uint64_t _requests_served = 0;
    uint64_t _read_errors = 0;
    uint64_t _respond_errors = 0;

    seastar::shared_ptr<seastar::tls::server_credentials> _credentials;
    seastar::gate _task_gate;

    std::optional<seastar::net::keepalive_params> _keepalive_params;
    size_t _content_length_limit = std::numeric_limits<size_t>::max();

    seastar::sstring _date = http_date();

    boost::intrusive::list<connection,
                           boost::intrusive::constant_time_size<false>>
        _connections;
    friend class shunyakv::connection;

  public:
    socket_server() = default;
    void set_handler(handler_factory_t handler) {
        _handler_factory = std::move(handler);
    }

    void set_tls_credentials(
        seastar::shared_ptr<seastar::tls::server_credentials> creds) {
        _credentials = std::move(creds);
    }

    void set_keepalive_parameters(
        std::optional<seastar::net::keepalive_params> params) {
        _keepalive_params = std::move(params);
    }

    size_t get_content_length_limit() const { return _content_length_limit; }
    void set_content_length_limit(size_t limit) {
        _content_length_limit = limit;
    }

    seastar::future<> listen(seastar::socket_address addr);
    seastar::future<> listen(seastar::socket_address addr,
                             seastar::listen_options lo);

    seastar::future<>
    listen(seastar::socket_address addr,
           seastar::shared_ptr<seastar::tls::server_credentials> creds);

    seastar::future<>
    listen(seastar::socket_address addr, seastar::listen_options lo,
           seastar::shared_ptr<seastar::tls::server_credentials> creds);

    seastar::future<> do_accepts(int which, bool with_tls);
    seastar::future<> do_accepts(int which);
    seastar::future<> do_accept_one(int which, bool with_tls);

    seastar::future<> stop();
    seastar::future<> start();

    uint64_t total_connections() const { return _total_connections; }
    uint64_t current_connections() const { return _current_connections; }
    uint64_t requests_served() const { return _requests_served; }
    uint64_t read_errors() const { return _read_errors; }
    uint64_t reply_errors() const { return _respond_errors; }

    static seastar::sstring http_date();

    // Server-side helpers for connection lifecycle (strongly recommended)
    void on_conn_open(connection &c);
    void on_conn_close(connection &c);
    seastar::gate &task_gate() { return _task_gate; }
    auto &connections() { return _connections; }
};
} // namespace shunyakv
