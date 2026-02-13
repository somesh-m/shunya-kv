#include <exception>
#include <iostream>

#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh> // input_stream/output_stream, temporary_buffer
#include <seastar/core/sstring.hh>
#include <seastar/net/api.hh> // connected_socket etc (often already via your headers)

#include "conn/connections.hh"
#include "conn/socket_server.hh"
#include <seastar/util/log.hh>

#include <seastar/core/when_all.hh>

using namespace seastar;
namespace shunyakv {
static logger conn_logger("connection");

void connection::on_new_connection() {
    _sock.set_nodelay(true);
    _server.on_conn_open(*this);
}

void connection::shutdown() {
    _sock.shutdown_input();
    _sock.shutdown_output();
}

connection::~connection() {
    --_server._current_connections;
    _server._connections.erase(_server._connections.iterator_to(*this));
}

}; // namespace shunyakv
