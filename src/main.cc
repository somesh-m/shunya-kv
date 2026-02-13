// #include "metrics_logger.hh"
#include "cmd_node_info.hh"
#include "config_loader.hh"
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

#include "conn/orchestrator.hh"
#include "conn/resp_handler.hh"
#include "conn/socket_handler.hh"
#include "dbconfig.hh"

using namespace seastar;
using namespace shunyakv;

static db_config config;
static logger m_log{"main"};
seastar::future<> accept_forever(seastar::server_socket, uint16_t,
                                 seastar::abort_source &);
seastar::future<> init();

seastar::future<> init() {
    // Config block
    static node_cfg cfg;
    cfg.base_port = config.db_port;
    // cfg.smp = seastar::smp::count;
    // cfg.port_offset = 1;
    cfg.hash = config.hash;
    // cfg.isPreHashed = true;
    shunyakv::set_node_cfg(cfg);

    uint16_t port = cfg.base_port;
    return seastar::do_with(
        socket_server_orchestrator{}, seastar::abort_source{},
        [port](socket_server_orchestrator &server, seastar::abort_source &as) {
            engine().handle_signal(SIGINT, [&as] { as.request_abort(); });
            engine().handle_signal(SIGTERM, [&as] { as.request_abort(); });

            return server.start()
                .then([port, &server] {
                    seastar::listen_options lo;
                    lo.lba = server_socket::load_balancing_algorithm::
                        connection_distribution;

                    return server
                        .set_handler([] {
                            return std::make_unique<RespHandler>();
                        })
                        .then([port, lo, &server]() mutable {
                            return server.listen(
                                socket_address(ipv4_addr{"0.0.0.0", port}), lo);
                        });
                })
                .then([&as] {
                    return seastar::sleep_abortable(
                               std::chrono::hours(24 * 365 * 100), as)
                        .handle_exception_type(
                            [](const seastar::sleep_aborted &) {});
                })
                .finally([&server] { return server.stop(); });
        });
}

int main(int argc, char **argv) {
    load_config_txt(config);
    app_template app;
    app.add_options()("port",
                      boost::program_options::value<uint16_t>(&config.db_port)
                          ->default_value(config.db_port),
                      "DB Port");
    try {
        app.run(argc, argv, init);
    } catch (...) {
        std::cerr << "Couldn't start application: " << std::current_exception()
                  << "\n";
        return 1;
    }
    return 0;
}
