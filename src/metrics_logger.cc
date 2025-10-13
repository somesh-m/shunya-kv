// metrics_logger.cc
#include "metrics_logger.hh"
#include <fmt/format.h> // for fmt::format (if not already pulled in)
#include <fstream>
#include <limits> // for std::numeric_limits
#include <numeric>
#include <optional>
#include <seastar/core/coroutine.hh>
#include <seastar/core/reactor.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/timer.hh>
#include <seastar/core/when_all.hh>
#include <sstream> // for std::istringstream
#include <string_view>
#include <vector>

static seastar::logger perf_log{"perf"};

namespace {

// ---------- CPU (/proc/stat) ----------
struct cpu_sample {
    uint64_t user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0,
             softirq = 0, steal = 0;
};

cpu_sample read_cpu_procstat() {
    // blocking read on shard 0 is fine for these tiny files
    std::ifstream f("/proc/stat");
    cpu_sample s{};
    std::string tag;
    if (f) {
        f >> tag; // "cpu"
        f >> s.user >> s.nice >> s.sys >> s.idle >> s.iowait >> s.irq >>
            s.softirq >> s.steal;
    }
    return s;
}

struct cpu_pct {
    double user = 0, sys = 0;
};
cpu_pct diff_cpu(const cpu_sample &a, const cpu_sample &b) {
    const auto du = (b.user - a.user) + (b.nice - a.nice);
    const auto ds = (b.sys - a.sys) + (b.irq - a.irq) + (b.softirq - a.softirq);
    const auto di = (b.idle - a.idle) + (b.iowait - a.iowait);
    const double tot = double(du + ds + di + (b.steal - a.steal));
    cpu_pct p{};
    if (tot > 0) {
        p.user = 100.0 * double(du) / tot;
        p.sys = 100.0 * double(ds) / tot;
    }
    return p;
}

// ---------- Run queue (PSI + procs_running) ----------
struct runq_sample {
    double psi10 = 0, psi60 = 0, psi300 = 0;
    int procs_running = 0;
};

runq_sample read_runq() {
    runq_sample r{};
    // /proc/pressure/cpu: "some avg10=0.00 avg60=0.00 avg300=0.00 total=..."
    {
        std::ifstream f("/proc/pressure/cpu");
        if (f) {
            std::string kind, avg10, avg60, avg300, total;
            f >> kind >> avg10 >> avg60 >> avg300 >> total;
            auto parse = [](const std::string &s) {
                auto pos = s.find('=');
                return pos == std::string::npos ? 0.0
                                                : std::stod(s.substr(pos + 1));
            };
            r.psi10 = parse(avg10);
            r.psi60 = parse(avg60);
            r.psi300 = parse(avg300);
        }
    }
    // /proc/stat line: "procs_running N"
    {
        std::ifstream f("/proc/stat");
        std::string key;
        while (f >> key) {
            if (key == "procs_running") {
                f >> r.procs_running;
                break;
            }
            // skip rest of the line
            f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
    }
    return r;
}

// ---------- Net (/sys/class/net/<iface>/statistics/*) ----------
struct net_sample {
    uint64_t rx_pkts = 0, tx_pkts = 0, rx_drop = 0, tx_drop = 0;
};

uint64_t read_u64_file(const std::string &p) {
    std::ifstream f(p);
    uint64_t v = 0;
    if (f)
        f >> v;
    return v;
}

std::string default_iface() {
    // parse /proc/net/route, pick iface where Destination == 00000000 (default
    // route)
    std::ifstream f("/proc/net/route");
    std::string line;
    std::getline(f, line); // header
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string iface, dest;
        ss >> iface >> dest;
        if (dest == "00000000")
            return iface;
    }
    return "eth0"; // fallback
}

net_sample read_net(const std::string &iface) {
    const std::string base = "/sys/class/net/" + iface + "/statistics/";
    return {
        read_u64_file(base + "rx_packets"),
        read_u64_file(base + "tx_packets"),
        read_u64_file(base + "rx_dropped"),
        read_u64_file(base + "tx_dropped"),
    };
}

struct net_rate {
    double rx_pps = 0, tx_pps = 0, rx_drop_s = 0, tx_drop_s = 0;
};
net_rate diff_net(const net_sample &a, const net_sample &b, double secs) {
    net_rate r{};
    if (secs > 0) {
        r.rx_pps = (b.rx_pkts - a.rx_pkts) / secs;
        r.tx_pps = (b.tx_pkts - a.tx_pkts) / secs;
        r.rx_drop_s = (b.rx_drop - a.rx_drop) / secs;
        r.tx_drop_s = (b.tx_drop - a.tx_drop) / secs;
    }
    return r;
}

} // namespace

namespace shunyakv {

seastar::future<> start_perf_logger(seastar::sharded<service> &svc,
                                    std::string iface) {
    if (seastar::this_shard_id() != 0)
        co_return; // only shard 0 logs

    if (iface.empty())
        iface = default_iface();
    perf_log.info("perf logger using iface={}", iface);

    // Initial baselines
    auto cpu_prev = read_cpu_procstat();
    auto net_prev = read_net(iface);
    auto ctr_prev = co_await svc.invoke_on_all(
        [](service &s) { return s.snapshot_counters(); });

    auto last = seastar::lowres_clock::now();
    while (true) {
        co_await seastar::sleep(std::chrono::seconds(1));
        const auto now = seastar::lowres_clock::now();
        const double secs = std::chrono::duration<double>(now - last).count();
        last = now;

        // CPU user/sys
        auto cpu_now = read_cpu_procstat();
        auto cpuP = diff_cpu(cpu_prev, cpu_now);
        cpu_prev = cpu_now;

        // Reactor busy per shard
        std::vector<seastar::future<double>> futs;
        futs.reserve(seastar::smp::count);
        for (unsigned s = 0; s < seastar::smp::count; ++s) {
            futs.emplace_back(seastar::smp::submit_to(
                s, [] { return seastar::engine().utilization(); }));
        }
        auto utils = co_await seastar::when_all_succeed(
            futs.begin(), futs.end()); // vector<double>

        // Run queue/PSI
        auto rq = read_runq();

        // Net deltas
        auto net_now = read_net(iface);
        auto netR = diff_net(net_prev, net_now, secs);
        net_prev = net_now;

        // Ops per shard (delta/s)
        auto ctr_now = co_await svc.invoke_on_all(
            [](service &s) { return s.snapshot_counters(); });

        // build one log line
        std::string util_str;
        util_str.reserve(seastar::smp::count * 6);
        for (unsigned s = 0; s < utils.size(); ++s) {
            util_str += fmt::format("{}:{:.1f}%", s, utils[s] * 100.0);
            if (s + 1 < utils.size())
                util_str += ",";
        }

        // ops/s per shard & keys
        std::string ops_str, keys_str;
        for (unsigned s = 0; s < ctr_now.size(); ++s) {
            const uint64_t d_get = ctr_now[s].get_ops - ctr_prev[s].get_ops;
            const uint64_t d_set = ctr_now[s].set_ops - ctr_prev[s].set_ops;

            const double get_s = double(d_get) / secs;
            const double set_s = double(d_set) / secs;

            ops_str +=
                fmt::format("{}:GET {:.0f},SET {:.0f}; ", s, get_s, set_s);
            keys_str += fmt::format("{}:{} ", s, ctr_now[s].keys_owned);
        }
        ctr_prev = std::move(ctr_now);

        perf_log.info("CPU user={:.1f}% sys={:.1f}% | reactor_busy[{}] | runq "
                      "procs_running={} psi10/60/300={:.2f}/{:.2f}/{:.2f} | "
                      "NET {} rx_pps={:.0f} tx_pps={:.0f} rx_drop/s={:.1f} "
                      "tx_drop/s={:.1f} | "
                      "OPS {} | KEYS {}",
                      cpuP.user, cpuP.sys, util_str, rq.procs_running, rq.psi10,
                      rq.psi60, rq.psi300, iface, netR.rx_pps, netR.tx_pps,
                      netR.rx_drop_s, netR.tx_drop_s, ops_str, keys_str);
    }
}

} // namespace shunyakv
