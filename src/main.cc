// #include "metrics_logger.hh"
#include "cmd_node_info.hh"
#include "server_debug.hh"
#include <chrono>
#include <seastar/core/abort_source.hh>
#include <seastar/core/app-template.hh>
#include <seastar/core/coroutine.hh>
#include <seastar/core/distributed.hh> // distributed<T>
#include <seastar/core/reactor.hh>
#include <seastar/core/sharded.hh>
#include <seastar/core/sleep.hh>
#include <seastar/core/when_all.hh>
#include <seastar/net/api.hh>

#include "router.hh" // service
#include "server.hh" // tcp_server

using namespace seastar;
using namespace shunyakv;

static logger m_log{"main"};
seastar::future<> accept_forever(seastar::server_socket, uint16_t,
                                 seastar::abort_source &);
seastar::future<> init();

seastar::future<> init() {
    // This logic should only run on shard 0
    if (this_shard_id() != 0) {
        co_return;
    }

    static seastar::abort_source as;
    static seastar::gate g;

    // Config block
    // TODO: Fetch this from config file
    static node_cfg cfg;
    cfg.base_port = 60110;
    cfg.smp = seastar::smp::count;
    cfg.port_offset = 1;
    cfg.hash = "fnv1a64";
    cfg.isPreHashed = true;
    shunyakv::set_node_cfg(cfg);

    // TODO: Add logic to find free ports and assign to shards
    const uint16_t base_port = 60110;
    const unsigned shard_count = smp::count;
    m_log.info("Starting shunyakv on {} shards", shard_count);

    // sharded creates a copy of the service on each logical core
    static sharded<service> store;
    static sharded<tcp_server> servers;

    // start store on all shards, store does the data handling part
    co_await store.start();
    co_await store.invoke_on_others(&service::start);

    // start tcp server on all sockets on shard 0
    // as per the architecture of seastar, shard 0 accepts all connections
    // and then dispatches the requests to other shards as per the selected
    // load balancing strategy
    // For our use case, we will used fixed strategy to route the requests
    // For each socket we will have a fixed shard to handle the request
    for (unsigned cpu = 0; cpu < shard_count; ++cpu) {
        const uint16_t port = base_port + cpu;
        seastar::listen_options lo;
        lo.reuse_address = true;
        lo.lba = server_socket::load_balancing_algorithm::fixed;
        lo.fixed_cpu = cpu;
        // Accept loop - continuation runs on the destination shard
        auto listener = listen(make_ipv4_address({"0.0.0.0", port}), lo);
        // Run each accept loop as its own task; gate gives you a clean
        // join.
        (void)seastar::with_gate(g, [l = std::move(listener), port]() mutable {
            return accept_forever(std::move(l), port, as);
        });
    }

    co_await servers.start(base_port, std::ref(store));
    co_await servers.invoke_on_others(&tcp_server::start);
}

int main(int argc, char **argv) {
    app_template app;
    try {
        app.run(argc, argv, init);
    } catch (...) {
        std::cerr << "Couldn't start application: " << std::current_exception()
                  << "\n";
        return 1;
    }
    return 0;
}

seastar::future<> accept_forever(seastar::server_socket listener, uint16_t port,
                                 seastar::abort_source &as) {
    try {
        return seastar::do_with(
            std::move(listener), std::move(port),
            [](auto &listener, auto &port) {
                return seastar::keep_doing([&listener, &port]() {
                    return listener.accept().then(
                        [&port](seastar::accept_result ar) {
                            m_log.info(
                                "Accepted on port {} -> shard {} from {}", port,
                                seastar::this_shard_id(), ar.remote_address);
                        });
                });
            });
    } catch (const seastar::abort_requested_exception &) {
        m_log.info("Accept loop on port {} aborted", port);
    } catch (const std::system_error &se) {
        m_log.error("accept() failed on port {}: {} ({})", port,
                    se.code().message(), se.code().value());
    }
}
