#include "cmd_node_info.hh"
#include "conn/orchestrator.hh"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>

#include "conn/resp_handler.hh"
#include <seastar/core/reactor.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/smp.hh>
#include <seastar/net/api.hh>
#include <seastar/testing/test_case.hh>

using namespace seastar;
using namespace shunyakv;

namespace {

class shard_echo_handler final : public PipelinedSocketHandler {
  protected:
    seastar::future<seastar::sstring>
    handle_request(shunyakv::ParsedRequest req) override {
        co_return seastar::format("{}:{}\n", seastar::this_shard_id(),
                                  req.frame);
    }

    std::optional<shunyakv::ParsedRequest>
    try_extract_request(seastar::sstring &buf) override {
        return PipelinedSocketHandler::try_extract_request(buf);
    }
};

class echo_handler final : public PipelinedSocketHandler {
  protected:
    seastar::future<seastar::sstring>
    handle_request(shunyakv::ParsedRequest req) override {
        co_return req.frame + "\n";
    }

    std::optional<shunyakv::ParsedRequest>
    try_extract_request(seastar::sstring &buf) override {
        return PipelinedSocketHandler::try_extract_request(buf);
    }
};

seastar::future<seastar::sstring> read_line(seastar::input_stream<char> &in) {
    seastar::sstring out;
    while (true) {
        auto chunk = co_await in.read();
        if (!chunk) {
            co_return out;
        }
        out.append(chunk.get(), chunk.size());
        const auto pos = out.find('\n');
        if (pos != seastar::sstring::npos) {
            co_return out.substr(0, pos + 1);
        }
    }
}

seastar::future<seastar::sstring> read_all(seastar::input_stream<char> &in) {
    seastar::sstring out;
    while (true) {
        auto chunk = co_await in.read();
        if (!chunk) {
            co_return out;
        }
        out.append(chunk.get(), chunk.size());
    }
}

seastar::future<uint16_t>
listen_on_available_port(socket_server_orchestrator &server,
                         seastar::listen_options lo) {
    const auto seed = static_cast<uint16_t>(
        35000 +
        (std::chrono::steady_clock::now().time_since_epoch().count() % 5000));
    for (uint16_t p = seed; p < static_cast<uint16_t>(seed + 250); ++p) {
        try {
            co_await server.listen(
                seastar::socket_address(seastar::ipv4_addr{"127.0.0.1", p}),
                lo);
            co_return p;
        } catch (const std::system_error &e) {
            if (e.code() == std::make_error_code(std::errc::address_in_use)) {
                continue;
            }
            throw;
        }
    }
    throw std::runtime_error("unable to find available test port");
}

seastar::future<seastar::sstring> issue_request(uint16_t port,
                                                const seastar::sstring &req) {
    auto sock = co_await seastar::engine().connect(
        seastar::socket_address(seastar::ipv4_addr{"127.0.0.1", port}));
    auto in = sock.input();
    auto out = sock.output();

    co_await out.write(req + "\r\n");
    co_await out.flush();
    auto response = co_await read_line(in);

    co_await out.close();
    co_await in.close();
    co_return response;
}

seastar::future<seastar::sstring>
issue_raw_request(uint16_t port, const seastar::sstring &req) {
    auto sock = co_await seastar::engine().connect(
        seastar::socket_address(seastar::ipv4_addr{"127.0.0.1", port}));
    auto in = sock.input();
    auto out = sock.output();

    co_await out.write(req);
    co_await out.flush();
    co_await out.close();
    auto response = co_await read_all(in);

    co_await in.close();
    co_return response;
}

} // namespace

SEASTAR_TEST_CASE(ORCHESTRATOR_START_STOP_HAPPY_PATH) {
    socket_server_orchestrator orch;
    co_await orch.start();
    co_await orch.stop();
}

SEASTAR_TEST_CASE(ORCHESTRATOR_SET_HANDLER_AND_SERVE_REQUESTS) {
    node_cfg cfg;
    cfg.smp = seastar::smp::count;
    cfg.port_offset = 0;
    set_node_cfg(cfg);

    socket_server_orchestrator orch;
    co_await seastar::smp::invoke_on_all(
        [] { set_send_shard_details_on_connect(false); });

    co_await orch.start();
    co_await seastar::futurize_invoke([&orch]() -> seastar::future<> {
        co_await orch.set_handler(
            [] { return std::make_unique<shard_echo_handler>(); });

        seastar::listen_options lo;
        lo.lba = seastar::server_socket::load_balancing_algorithm::
            connection_distribution;
        const auto port = co_await listen_on_available_port(orch, lo);

        // Allow accept loops to start before first client connects.
        co_await seastar::sleep(std::chrono::milliseconds(20));

        const unsigned attempts =
            std::max<unsigned>(64, seastar::smp::count * 16);
        std::set<unsigned> seen_shards;

        for (unsigned i = 0; i < attempts; ++i) {
            const auto msg = seastar::format("msg{}", i);
            const auto response = co_await issue_request(port, msg);

            const auto colon = response.find(':');
            BOOST_REQUIRE(colon != seastar::sstring::npos);

            const auto sid = static_cast<unsigned>(
                std::stoul(std::string(response.substr(0, colon))));
            seen_shards.insert(sid);

            BOOST_REQUIRE_EQUAL(response.substr(colon + 1), msg + "\n");
        }

        // On loopback, connection distribution is not guaranteed to hit every
        // shard in a bounded number of attempts. Require spread across at
        // least two shards on multi-core setups.
        const size_t min_expected_shards = seastar::smp::count > 1 ? 2 : 1;
        BOOST_REQUIRE_GE(seen_shards.size(), min_expected_shards);
        BOOST_REQUIRE_LE(seen_shards.size(), seastar::smp::count);
    }).finally([&orch] { return orch.stop(); });
}

SEASTAR_TEST_CASE(orchestrator_multiple_start_stop) {
    socket_server_orchestrator orch;
    co_await orch.start();
    co_await orch.start();

    // Make sure the server is still listening after calling start() multiple
    // times. This will confirm that server isn't crashing due to this
    co_await seastar::futurize_invoke([&orch]() -> seastar::future<> {
        co_await orch.set_handler(
            [] { return std::make_unique<echo_handler>(); });

        seastar::listen_options lo;
        lo.lba = seastar::server_socket::load_balancing_algorithm::
            connection_distribution;
        const auto port = co_await listen_on_available_port(orch, lo);

        // Allow accept loops to start before first client connects.
        co_await seastar::sleep(std::chrono::milliseconds(20));
        const auto msg = seastar::format("hello");
        const auto response = co_await issue_request(port, msg);
        BOOST_REQUIRE_EQUAL(response, msg + "\n");
    }).finally([&orch] {
        return orch.stop().finally([&orch] { return orch.stop(); });
    });

    co_return;
}

SEASTAR_TEST_CASE(orchestrator_protocol_sanity) {
    socket_server_orchestrator orch;
    co_await orch.start();
    co_await orch.start();

    // Make sure the server is still listening after calling start() multiple
    // times. This will confirm that server isn't crashing due to this
    co_await seastar::futurize_invoke([&orch]() -> seastar::future<> {
        co_await orch.set_handler(
            [] { return std::make_unique<RespHandler>(); });

        seastar::listen_options lo;
        lo.lba = seastar::server_socket::load_balancing_algorithm::
            connection_distribution;
        const auto port = co_await listen_on_available_port(orch, lo);

        // Allow accept loops to start before first client connects.
        co_await seastar::sleep(std::chrono::milliseconds(20));
        const auto msg =
            seastar::format("*2\r\n$9\r\nNODE_INFO\r\n$3\r\nMAP\r\n");
        const auto response = co_await issue_raw_request(port, msg);
        const auto expected_json = seastar::sstring(
            "{\"epoch\":1,\"smp\":1,\"base_port\":60110,\"port_offset\":0,"
            "\"hash\":\"fnv1a435345\",\"ranges\":[[\"0\","
            "\"18446744073709551615\",0,60110]]}");
        const auto expected_response = seastar::format(
            "${}\r\n{}\r\n", expected_json.size(), expected_json);
        BOOST_REQUIRE(!response.empty());
        BOOST_REQUIRE_EQUAL(response, expected_response);
    }).finally([&orch] {
        return orch.stop().finally([&orch] { return orch.stop(); });
    });

    co_return;
}
