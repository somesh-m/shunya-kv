#pragma once

#include <seastar/core/circular_buffer.hh>
#include <seastar/core/condition-variable.hh>
#include <seastar/core/future.hh>
#include <seastar/core/gate.hh>
#include <seastar/core/semaphore.hh>
#include <seastar/core/shared_ptr.hh>
#include <seastar/core/sstring.hh>
#include <seastar/net/api.hh>
#include <seastar/net/tls.hh>

#include <boost/intrusive/list.hpp>
#include <limits>
#include <optional>
#include <vector>

using namespace seastar;
namespace shunyakv {

class socket_server; // forward declaration

class connection
    : public boost::intrusive::list_base_hook<
          boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {
  private:
    socket_server &_server;

    seastar::connected_socket _sock;
    seastar::input_stream<char> _in;
    seastar::output_stream<char> _out;

    seastar::socket_address _client_addr;
    seastar::socket_address _server_addr;

    seastar::circular_buffer<seastar::future<seastar::sstring>> _respq;
    seastar::condition_variable _respq_cv;

    // Backpressure: limit in-flight requests queued per connection
    static constexpr size_t max_inflight = 1024;
    seastar::semaphore _slots{max_inflight};

    bool _eof = false;

    bool _done = false;
    bool _tls = false;

    // TODO: Implement handling client id
    seastar::sstring client_id;

    // Your “business logic” (echo for now)
    seastar::future<seastar::sstring> handle_line(seastar::sstring line);

  public:
    connection(socket_server &server, seastar::connected_socket &&sock,
               bool tls)
        : _server(server), _sock(std::move(sock)), _in(_sock.input()),
          _out(_sock.output()), _client_addr(_sock.remote_address()),
          _server_addr(_sock.local_address()), _tls(tls) {
        on_new_connection();
    }

    connection(socket_server &server, seastar::connected_socket &&fd,
               seastar::socket_address, bool tls)
        : connection(server, std::move(fd), tls) {}

    connection(socket_server &server, seastar::connected_socket &&fd,
               seastar::socket_address client_addr,
               seastar::socket_address server_addr, bool tls)
        : _server(server), _sock(std::move(fd)), _in(_sock.input()),
          _out(_sock.output()), _client_addr(std::move(client_addr)),
          _server_addr(std::move(server_addr)), _tls(tls) {
        on_new_connection();
    }

    ~connection() noexcept;

    void on_new_connection();
    seastar::input_stream<char> &in() { return _in; }
    seastar::output_stream<char> &out() { return _out; }

    future<> read();
    future<> write();
    // future<> process();
    void shutdown();
};
} // namespace shunyakv
