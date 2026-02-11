#include "server.hh"
#include "commands.hh"
#include "proto_helpers.hh"
#include "protocol.hh"

#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include <fcntl.h> // open flags
#include <seastar/core/app-template.hh>
#include <seastar/core/print.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/when_all.hh>
#include <seastar/net/api.hh>
#include <seastar/util/defer.hh>
#include <sys/syscall.h> // SYS_gettid
#include <unistd.h>      // syscall, write, close

#include <resp/resp_parser.hh>
#include <resp/resp_writer.hh>

using namespace seastar;

static logger s_log{"server"};
extern unsigned shard_for(std::string_view key); // your mapper

namespace shunyakv {

future<> tcp_server::handle_session(connected_socket s, socket_address peer,
                                    shunyakv::service &store) {
    s_log.info("accepted connection from {} (shard {})", peer, this_shard_id());
    auto *ps = &store;
    s_log.info("session on shard {} using service {}", seastar::this_shard_id(),
               fmt::ptr(ps));

    auto in = input_stream<char>(s.input());
    auto out = output_stream<char>(s.output());

    bool done = false;
    resp::Reader reader;

    try {

        while (!done) {

            // Read one RESP command
            auto cmd = co_await reader.read_command(in);

            if (cmd.empty()) {
                co_return;
            }

            auto cmdU = shunyakv::proto::to_upper(cmd[0]);

            const auto &table = command_dispatch();
            auto it = table.find(cmdU);
            if (it == table.end()) {
                co_await resp::write_error(out, "ERR unknown command");
                co_await out.flush();
                continue;
            }

            co_await it->second(cmd, out, store);

            if (cmdU == "QUIT") {
                done = true;
            }

            co_await out.flush();
        }
    } catch (const std::exception &ex) {
        s_log.warn("session error ({}): {}", this_shard_id(), ex.what());
    }
}

seastar::future<> tcp_server::start() {
    _running = true;
    static seastar::abort_source _as;
    static seastar::gate _gate;
    const auto sid = seastar::this_shard_id();
    const uint16_t port = static_cast<uint16_t>(_port + sid);

    seastar::listen_options lo;
    lo.reuse_address = true; // safer for quick restarts

    auto listener =
        seastar::listen(seastar::make_ipv4_address({"0.0.0.0", port}), lo);
    s_log.info("Listening on port {} shard {}", port, sid);

    try {
        while (!_as.abort_requested()) {
            // co_await accept with abort support; yields the reactor
            auto ar = co_await listener.accept();

            // capture what you need before moving
            auto peer = ar.remote_address;
            auto &store = _store.local();

            ar.connection.set_nodelay(true);

            // spawn a fiber for this session, tracked by the gate
            (void)seastar::with_gate(_gate, [this,
                                             conn = std::move(ar.connection),
                                             peer = std::move(peer),
                                             &store]() mutable {
                (void)handle_session(std::move(conn), std::move(peer), store);
            });
        }
    } catch (const seastar::abort_requested_exception &) {
        s_log.info("Accept loop aborted on port {} shard {}", port, sid);
    } catch (const std::system_error &se) {
        s_log.error("Accept loop error on port {} shard {}: {} ({})", port, sid,
                    se.code().message(), se.code().value());
    } catch (const std::exception &e) {
        s_log.error("Accept loop error on port {} shard {}: {}", port, sid,
                    e.what());
    }

    // listener destroyed here (closes socket)
    co_return;
}

future<> tcp_server::stop() {
    s_log.warn("STOP shard={} requested", seastar::this_shard_id());
    // _running = false;
    // _as.request_abort();    // unblocks accept()
    // co_await _gate.close(); // wait for sessions
    // _listener = {};         // close listen socket
    // s_log.warn("STOP shard={} complete", seastar::this_shard_id());
    co_return;
}

} // namespace shunyakv
