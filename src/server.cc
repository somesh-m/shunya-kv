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

// tcp server start
future<> tcp_server::start() {
    _running = true;
    const auto sid = seastar::this_shard_id(); // gets the current shard id
    const auto port = uint16_t(
        _port + sid); // finds the port by adding the shard id to base port
    std::cout << "Port from individual listener " << port << "\n";
    seastar::listen_options lo;
    lo.reuse_address = false;
    return seastar::do_with(
               seastar::listen(make_ipv4_address({"0.0.0.0", port}), lo),
               [this, port, sid](seastar::server_socket &listener) {
                   s_log.info("Listening on port {} shard {}", port, sid);
                   s_log.info("Analyzing server socket port {} ",
                              listener.local_address().port());
                   // This will resolve only on failure of the accept() call
                   // On success, the loop continues forever
                   // We don't need to move or copy listener because
                   // do_with ensures it stays alive
                   // until the lambda returns
                   // The lambda itself runs forever unless accept()
                   // fails
                   return seastar::keep_doing([this, &listener]() {
                              s_log.info("Came to conn loop");
                              return listener.accept()
                                  .then([this, &listener](
                                            seastar::accept_result res) {
                                      s_log.info("Accepted connection from "
                                                 "remote address");
                                      auto peer = res.remote_address;
                                      auto &store = this->_store.local();
                                      res.connection.set_nodelay(true);
                                      return handle_session(
                                          std::move(res.connection),
                                          std::move(peer), store);
                                  })
                                  .handle_exception([](std::exception_ptr e) {
                                      s_log.info("Accept failed: {}", e);
                                  });
                          })
                       .finally([&listener] {
                           // listener is still alive here
                           s_log.info("Listener on port {} shard {} closing",
                                      listener.local_address().port(),
                                      seastar::this_shard_id());
                           // close the listen socket
                           listener = {};
                       });
               })
        .finally([port, sid] {
            s_log.info("Listener on port {} shard {} exiting", port, sid);
        });
}
// auto listener = std::make_unique<seastar::server_socket>(
//     seastar::listen(make_ipv4_address({"0.0.0.0", port}), lo));
// return seastar::do_with()
// auto *ls = listener.get();
// s_log.info("listening on port {} (shard {})", port, sid);

// accept loop
// keep_doing enables us to repeatedly accept new connections
// keep_doing exits only in case of exceptions
// (void)seastar::keep_doing([this, ls] {
//     // ls->accept() is used to accept a new connection
//     return ls->accept().then([this](accept_result ar) {
//         auto s = std::move(ar.connection);
//         auto a = std::move(ar.remote_address);
//         pid_t pid = ::getpid();
//         pid_t tid = (pid_t)::syscall(SYS_gettid);
//         s_log.debug("ACCEPT shard={} tid={} local={} peer={}",
//                     seastar::this_shard_id(), tid, s.local_address(),
//                     ar.remote_address);
//         auto &store = _store.local();

//         (void)handle_session(std::move(s), std::move(a), store)
//             .handle_exception([](std::exception_ptr ep) {
//                 try {
//                     std::rethrow_exception(ep);
//                 } catch (const std::exception &ex) {
//                     s_log.error("session error: {}\n", ex.what());
//                 }
//             });
//     });
// });

// co_return;

// return seastar::keep_doing([this] {
//     return _listener.accept()
//         .then([this](accept_result ar) {
//             auto s = std::move(ar.connection);
//             auto a = std::move(ar.remote_address);

//             // run session in background but track it in a gate
//             (void)seastar::with_gate(
//                 _sessions, handle_session(std::move(s), std::move(a))
//                                .handle_exception([](std::exception_ptr
//                                ep) {
//                                    try {
//                                        std::rethrow_exception(ep);
//                                    } catch (const std::exception &ex) {
//                                        fmt::print("session error: {}\n",
//                                                   ex.what());
//                                    }
//                                }));
//             // lambda returns void → futurized; loop immediately
//             // continues
//             // to next accept()
//         })
//         .handle_exception([this](std::exception_ptr ep) {
//             try {
//                 std::rethrow_exception(ep);
//             } catch (const std::exception &ex) {
//                 s_log.error("accept() error: {}", ex.what());
//             }
//         });
// });
// }

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
