#pragma once
#include "dbconfig.hh"
#include "kv_store.hh"
#include "router_metrics.hh"

#include "hotpath_metrics.hh"
#include "shard_stats.hh"
#include <cstdint>
#include <functional>
#include <optional>
#include <seastar/core/future.hh>
#include <seastar/core/smp.hh>
#include <seastar/core/sstring.hh>
#include <seastar/core/temporary_buffer.hh>
#include <seastar/net/api.hh>
#include <string_view>

using namespace seastar;
namespace shunyakv {

class service {
  public:
    future<> start(const db_config &cfg);
    future<> stop();
    future<bool> local_set(std::string_view, sstring);
    future<bool> local_set(std::string_view, sstring, uint64_t);
    future<std::optional<sstring>> local_get(std::string_view);
    void record_get(bool) noexcept;
    void record_set(bool) noexcept;
    void record_get_latency(uint64_t) noexcept;
    void record_set_latency(uint64_t) noexcept;
    void record_cache_miss() noexcept;
    request_counters snapshot_request_counters() const noexcept;
    request_latency_counters snapshot_request_latency_counters() const noexcept;
    shard_stats_snapshot snapshot_shard_stats() const noexcept;

  private:
    store _store;
    request_counters _req_counters;
    request_latency_counters _latency_counters;
};

inline service &local_service() {
    static thread_local service svc;
    return svc;
}

} // namespace shunyakv
