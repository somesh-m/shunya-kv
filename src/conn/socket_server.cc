#include "conn/socket_server.hh"
#include <seastar/core/do_with.hh>
#include <seastar/core/loop.hh>
#include <seastar/core/reactor.hh>
#include <seastar/net/api.hh>
#include <seastar/util/log.hh>

using namespace seastar;
namespace shunyakv {

static seastar::logger socket_logger("socker_server");

void socket_server::on_conn_open(connection &c) {
    ++_total_connections;
    ++_current_connections;
    _connections.push_back(c);
}

void socket_server::on_conn_close(connection &) { --_current_connections; }

seastar::future<> socket_server::listen(
    seastar::socket_address addr, seastar::listen_options lo,
    seastar::shared_ptr<seastar::tls::server_credentials> creds) {
    lo.reuse_address = true;
    _listeners.push_back(seastar::listen(addr, lo));
    // Start accept loop for this listener (don’t block other work)
    return do_accepts(_listeners.size() - 1, creds != nullptr);
}

seastar::future<> socket_server::listen(seastar::socket_address addr,
                                        seastar::listen_options lo) {
    return listen(addr, lo, _credentials);
}

seastar::future<> socket_server::listen(seastar::socket_address addr) {
    seastar::listen_options lo;
    lo.reuse_address = true;
    return listen(addr, lo, _credentials);
}

seastar::future<> socket_server::listen(
    seastar::socket_address addr,
    seastar::shared_ptr<seastar::tls::server_credentials> creds) {
    seastar::listen_options lo;
    lo.reuse_address = true;
    return listen(addr, lo, creds);
}

future<> socket_server::do_accepts(int which, bool tls) {
    (void)try_with_gate(_task_gate, [this, which, tls] {
        return keep_doing([this, which, tls] {
                   return try_with_gate(_task_gate, [this, which, tls] {
                       return do_accept_one(which, tls);
                   });
               })
            .handle_exception_type([](const gate_closed_exception &e) {});
    }).handle_exception_type([](const gate_closed_exception &e) {});
    return make_ready_future<>();
}

future<> socket_server::do_accepts(int which) {
    return do_accepts(which, _credentials != nullptr);
}

future<> socket_server::do_accept_one(int which, bool tls) {
    return _listeners[which]
        .accept()
        .then([this, tls](accept_result ar) mutable {
            auto handler = _handler_factory ? _handler_factory() : nullptr;
            if (!handler) {
                throw std::runtime_error("No handler configured");
            }
            if (_keepalive_params) {
                ar.connection.set_keepalive(true);
                ar.connection.set_keepalive_parameters(
                    _keepalive_params.value());
            }
            auto local_address = ar.connection.local_address();
            auto conn = std::make_unique<connection>(
                *this, std::move(ar.connection), std::move(ar.remote_address),
                std::move(local_address), tls);
            (void)try_with_gate(_task_gate, [conn = std::move(conn),
                                             handler =
                                                 std::move(handler)]() mutable {
                return handler->process(*conn)
                    .handle_exception([](std::exception_ptr ex) {
                        socket_logger.error("request error: {}", ex);
                    })
                    .finally([conn = std::move(conn),
                              handler = std::move(handler)]() mutable {});
            }).handle_exception_type([](const gate_closed_exception &e) {});
        })
        .handle_exception_type([](const std::system_error &e) {
            // We expect a ECONNABORTED when socket_server::stop is called,
            // no point in warning about that.
            if (e.code().value() != ECONNABORTED) {
                socket_logger.error("accept failed: {}", e);
            }
        })
        .handle_exception([](std::exception_ptr ex) {
            socket_logger.error("accept failed: {}", ex);
        });
}

seastar::future<> socket_server::stop() {
    for (auto &&l : _listeners) {
        l.abort_accept();
    }
    for (auto &&c : _connections) {
        c.shutdown();
    }
    return _task_gate.close();
}
sstring socket_server::http_date() {
    auto t = ::time(nullptr);
    struct tm tm;
    gmtime_r(&t, &tm);
    // Using strftime() would have been easier, but unfortunately relies on
    // the current locale, and we need the month and day names in English.
    static const char *days[] = {"Sun", "Mon", "Tue", "Wed",
                                 "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    return seastar::format("{}, {:02d} {} {} {:02d}:{:02d}:{:02d} GMT",
                           days[tm.tm_wday], tm.tm_mday, months[tm.tm_mon],
                           1900 + tm.tm_year, tm.tm_hour, tm.tm_min, tm.tm_sec);
}

} // namespace shunyakv
