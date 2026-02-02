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

    size_t pending_bytes = 0;
    unsigned pending_replies = 0;

    static constexpr size_t FLUSH_BYTES = 64 * 1024; // tune
    static constexpr unsigned FLUSH_EVERY = 256;     // tune

    bool done = false;
    seastar::sstring carry; // holds leftover bytes after the last newline

    try {
        // co_await out.write("OK\r\n");
        // co_await out.flush();

        while (!done) {
            // Fill 'carry' until we see a '\n' or the peer closes
            while (carry.find('\n') == seastar::sstring::npos) {
                auto chunk =
                    co_await in.read(); // returns temporary_buffer<char>
                if (chunk.empty()) {
                    // connection closed; if no complete line, exit
                    if (carry.empty()) {
                        co_return;
                    }
                    // no newline but leftover data: treat it as a final
                    // line
                    carry.append("\n", 1);
                    break;
                }
                carry.append(chunk.get(), chunk.size());
            }

            // Extract one line
            auto nl = carry.find('\n');
            seastar::sstring line = carry.substr(0, nl);
            carry.erase(carry.begin(),
                        carry.begin() + nl + 1); // keep remaining data

            // Trim CR and spaces
            std::string_view sv{line.data(), line.size()};
            sv = shunyakv::proto::trim(sv);
            if (sv.empty()) {
                continue;
            }

            auto [cmd_sv, rest_sv] = shunyakv::proto::split_first(sv);
            auto cmdU = shunyakv::proto::to_upper(cmd_sv);

            const auto &table = command_dispatch();
            auto it = table.find(cmdU);
            if (it == table.end()) {
                co_await out.write("ERR unknown command\r\n");
                co_await out.flush();
                continue;
            }

            // Dispatch to handler
            co_await it->second(rest_sv, out, store);

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
                return handle_session(std::move(conn), std::move(peer), store);
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
