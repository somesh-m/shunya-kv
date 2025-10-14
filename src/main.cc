// #include "metrics_logger.hh"
#include "cmd_node_info.hh"
#include "server_debug.hh"
#include <chrono>
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
int main(int argc, char **argv) {
    app_template app;

    return app.run(argc, argv, [&]() -> future<> {
        // This logic should only run on shard 0
        if (this_shard_id() != 0) {
            co_return;
        }

        // Config block
        // TODO: Fetch this from config file
        static node_cfg cfg;
        cfg.base_port = 60110;
        cfg.smp = seastar::smp::count;
        cfg.port_offset = 1;
        cfg.hash = "fnv1a64";
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
        // as per the architecture of seastat, shard 0 accepts all connections
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
            auto server_socket =
                listen(make_ipv4_address({"0.0.0.0", port}), lo);

            (void)seastar::keep_doing(
                [server_socket = std::move(server_socket), port]() mutable {
                    return server_socket.accept()
                        .then([port](seastar::accept_result ar) {
                            auto sid = this_shard_id(); // destination shard id
                            std::cout << "Accepted on port " << port
                                      << " -> shard " << sid << "\n";
                            auto peer = ar.remote_address;
                            std::cout << "Remote address: " << peer << "\n";
                            // return store.local().handle_session(
                            //     std::move(ar.connection), peer, port);
                        })
                        .handle_exception([](std::exception_ptr e) {
                            std::cout << "Accept failed: " << e << "\n";
                        });
                });
        }

        co_await servers.start(base_port, std::ref(store));
        co_await servers.invoke_on_others(&tcp_server::start);
        std::cout << "Started shunyakv on " << shard_count << " shards" << "\n";

        co_await seastar::sleep(std::chrono::hours(24));
    });
}
