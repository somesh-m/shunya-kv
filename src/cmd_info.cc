#include "cmd_info.hh"
#include "commands.hh"
#include "router.hh"

#include <resp/resp_types.hh>
#include <resp/resp_writer.hh>

#include <chrono>
#include <hash.hh>
#include <optional>
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/memory.hh>
#include <seastar/core/print.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/when_all.hh>
#include <seastar/util/log.hh>
#include <string>

namespace shunyakv {

struct STATS {
    uint64_t total_allocs;
    size_t free_mem;
    size_t allocated_mem;
    size_t total_mem;
    uint64_t failed_alloc;
    shard_stats_snapshot shard_stats;
};

namespace {

STATS collect_statistics() {
    STATS stats;
    auto mem_stats = seastar::memory::stats();
    stats.allocated_mem = mem_stats.allocated_memory();
    stats.total_allocs = mem_stats.mallocs();
    stats.free_mem = mem_stats.free_memory();
    stats.total_mem = mem_stats.total_memory();
    stats.failed_alloc = mem_stats.failed_allocations();
    stats.shard_stats = shunyakv::local_service().snapshot_shard_stats();
    return stats;
}

seastar::sstring format_bytes(size_t bytes) {
    static constexpr const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }

    if (unit == 0) {
        return seastar::format("{} {} ({} bytes)", bytes, units[unit], bytes);
    }
    return seastar::format("{:.2f} {} ({} bytes)", value, units[unit], bytes);
}

} // namespace

static seastar::logger info_logger{"cmd_info"};

seastar::future<> handle_info(const resp::ArgvView &cmd,
                              seastar::output_stream<char> &out,
                              shunyakv::service & /*store*/) {
    // command syntax check
    if (cmd.size() != 1) {
        co_await resp::write_error(out,
                                   "ERR wrong number of arguements for 'INFO'");
        co_return;
    }

    std::vector<seastar::future<STATS>> futures;
    futures.reserve(seastar::smp::count);
    for (unsigned shard = 0; shard < seastar::smp::count; ++shard) {
        futures.emplace_back(seastar::smp::submit_to(
            shard, [] { return collect_statistics(); }));
    }

    auto per_shard =
        co_await seastar::when_all_succeed(futures.begin(), futures.end());

    STATS total{};
    std::string payload;
    payload.reserve(256 + per_shard.size() * 128);
    payload += "# Memory\r\n";

    for (unsigned shard = 0; shard < per_shard.size(); ++shard) {
        const auto &stats = per_shard[shard];
        total.total_allocs += stats.total_allocs;
        total.free_mem += stats.free_mem;
        total.allocated_mem += stats.allocated_mem;
        total.total_mem += stats.total_mem;
        total.failed_alloc += stats.failed_alloc;
        // total.cache_miss += stats.cache_miss;
        // total.eviction_count += stats.eviction_count;

        payload += seastar::format(
            "shard_{}\nallocated_memory: {}\nfree_memory: {}\ntotal_memory: "
            "{}\ntotal_allocs: {}\nfailed_allocs: {}\npool_total_slots: "
            "{}\npool_available_slots: {}\npool_fallback_allocs: "
            "{}\nkey_count: {}\ncache_miss: {}\neviction_count: {}\n\n",
            shard, format_bytes(stats.allocated_mem),
            format_bytes(stats.free_mem), format_bytes(stats.total_mem),
            stats.total_allocs, stats.failed_alloc,
            stats.shard_stats.pool_total_slots,
            stats.shard_stats.pool_available_slots,
            stats.shard_stats.pool_fallback_allocs, stats.shard_stats.key_count,
            stats.shard_stats.cache_miss, stats.shard_stats.eviction_count);
    }

    payload += "# Cumulative\r\n";
    payload += seastar::format(
        "allocated_memory: {}\nfree_memory: {}\ntotal_memory: {}\n"
        "total_allocs: {}\nfailed_allocs: {}\n",
        format_bytes(total.allocated_mem), format_bytes(total.free_mem),
        format_bytes(total.total_mem), total.total_allocs, total.failed_alloc);

    co_await resp::write_bulk(out, seastar::sstring(payload));
    co_return;
}

} // namespace shunyakv
