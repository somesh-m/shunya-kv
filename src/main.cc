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
#include <seastar/core/smp.hh>
#include <seastar/core/when_all.hh>
#include <seastar/net/api.hh>

#include "router.hh" // service
#include "server.hh" // tcp_server

#include "conn/orchestrator.hh"
#include "conn/resp_handler.hh"
#include "conn/socket_handler.hh"
#include "dbconfig.hh"
// - DENABLE_HOT_PATH_METRICS = OFF
// - DENABLE_REQUEST_COUNTERS = OFF
// -DENABLE_FORWARDED_REQUEST_COUNTERS=OFF
// -DENABLE_REQUEST_COUNTERS=OFF

using namespace seastar;
using namespace shunyakv;

static db_config config;
static logger m_log{"main"};
seastar::future<> accept_forever(seastar::server_socket, uint16_t,
                                 seastar::abort_source &);
seastar::future<> init();

struct shard_shutdown_stats {
    request_counters req;
    request_latency_counters latency;
};

static seastar::future<> log_request_counters_on_shutdown(
    std::chrono::steady_clock::time_point run_start) {
    std::vector<seastar::future<shard_shutdown_stats>> futs;
    futs.reserve(seastar::smp::count);
    for (unsigned sid = 0; sid < seastar::smp::count; ++sid) {
        futs.emplace_back(seastar::smp::submit_to(sid, [] {
            auto &svc = shunyakv::local_service();
            return shard_shutdown_stats{
                svc.snapshot_request_counters(),
                svc.snapshot_request_latency_counters()};
        }));
    }

    auto per_shard =
        co_await seastar::when_all_succeed(futs.begin(), futs.end());

#if SHUNYAKV_ENABLE_REQUEST_COUNTERS
    uint64_t get_total = 0;
    uint64_t set_total = 0;
#if SHUNYAKV_ENABLE_FORWARDED_REQUEST_COUNTERS
    uint64_t get_forwarded = 0;
    uint64_t set_forwarded = 0;
#endif
#endif
    request_latency_counters merged_latency;

    for (unsigned sid = 0; sid < per_shard.size(); ++sid) {
#if SHUNYAKV_ENABLE_REQUEST_COUNTERS
        const auto &c = per_shard[sid].req;
        get_total += c.get_total;
        set_total += c.set_total;
#if SHUNYAKV_ENABLE_FORWARDED_REQUEST_COUNTERS
        get_forwarded += c.get_forwarded;
        set_forwarded += c.set_forwarded;
#endif
#if SHUNYAKV_ENABLE_FORWARDED_REQUEST_COUNTERS
        m_log.info("shard {} request counters: GET total={} forwarded={} | SET "
                   "total={} forwarded={}",
                   sid, c.get_total, c.get_forwarded, c.set_total,
                   c.set_forwarded);
#else
        m_log.info("shard {} request counters: GET total={} | SET total={}",
                   sid, c.get_total, c.set_total);
#endif
#endif
        merged_latency.merge_from(per_shard[sid].latency);
    }

#if SHUNYAKV_ENABLE_REQUEST_COUNTERS
#if SHUNYAKV_ENABLE_FORWARDED_REQUEST_COUNTERS
    m_log.info("cluster request counters: GET total={} forwarded={} | SET "
               "total={} forwarded={}",
               get_total, get_forwarded, set_total, set_forwarded);
#else
    m_log.info("cluster request counters: GET total={} | SET total={}",
               get_total, set_total);
#endif

    const uint64_t total_ops = get_total + set_total;
    const auto elapsed_s =
        std::chrono::duration_cast<std::chrono::duration<double>>(
            std::chrono::steady_clock::now() - run_start)
            .count();
    const double safe_elapsed_s = elapsed_s > 0.0 ? elapsed_s : 1.0;
    const double qps = static_cast<double>(total_ops) / safe_elapsed_s;
    const double get_qps = static_cast<double>(get_total) / safe_elapsed_s;
    const double set_qps = static_cast<double>(set_total) / safe_elapsed_s;
    m_log.info("cluster throughput: uptime_s={:.3f} total_ops={} qps={:.3f} "
               "get_qps={:.3f} set_qps={:.3f}",
               elapsed_s, total_ops, qps, get_qps, set_qps);
#endif

    const auto us_to_ms = [](uint64_t us) {
        return static_cast<double>(us) / 1000.0;
    };
    const auto log_pct = [&](const char *label, const latency_histogram &h) {
        m_log.info(
            "{} n={} p50={:.3f}ms p90={:.3f}ms p99={:.3f}ms p99.9={:.3f}ms",
            label, h.count, us_to_ms(h.quantile_us(0.50)),
            us_to_ms(h.quantile_us(0.90)), us_to_ms(h.quantile_us(0.99)),
            us_to_ms(h.quantile_us(0.999)));
    };

    log_pct("OPS latency", merged_latency.total);
    co_return;
}

seastar::future<> init() {
    // Config block
    static node_cfg cfg;
    cfg.base_port = config.db_port;
    cfg.smp = seastar::smp::count;
    cfg.port_offset = 0;
    cfg.hash = config.hash;
    // cfg.isPreHashed = true;
    shunyakv::set_node_cfg(cfg);

    uint16_t port = cfg.base_port;
    const auto run_start = std::chrono::steady_clock::now();
    return seastar::do_with(
        socket_server_orchestrator{}, seastar::abort_source{},
        [port, run_start](socket_server_orchestrator &server,
                          seastar::abort_source &as) {
            engine().handle_signal(SIGINT, [&as] { as.request_abort(); });
            engine().handle_signal(SIGTERM, [&as] { as.request_abort(); });

            return seastar::smp::invoke_on_all(
                       [enabled = config.send_shard_details_on_connect] {
                           shunyakv::set_send_shard_details_on_connect(enabled);
                       })
                .then([&server, port] {
                    m_log.info("send_shard_details_on_connect={}",
                               config.send_shard_details_on_connect);
                    return server.start().then([port, &server] {
                        seastar::listen_options lo;
                        lo.lba = server_socket::load_balancing_algorithm::
                            connection_distribution;

                        return server
                            .set_handler(
                                [] { return std::make_unique<RespHandler>(); })
                            .then([port, lo, &server]() mutable {
                                return server.listen(
                                    socket_address(ipv4_addr{"0.0.0.0", port}),
                                    lo);
                            });
                    });
                })
                .then([&as] {
                    return seastar::sleep_abortable(
                               std::chrono::hours(24 * 365 * 100), as)
                        .handle_exception_type(
                            [](const seastar::sleep_aborted &) {});
                })
                .finally([&server, run_start] {
                    return server.stop().then([run_start] {
                        return log_request_counters_on_shutdown(run_start);
                    });
                });
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
