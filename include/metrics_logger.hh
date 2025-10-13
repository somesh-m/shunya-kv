// metrics_logger.hh
#pragma once
#include "router.hh" // service + shard_counters
#include <seastar/core/future.hh>
#include <seastar/core/sharded.hh>
#include <string>

namespace shunyakv {
seastar::future<> start_perf_logger(seastar::sharded<service> &svc,
                                    std::string iface = "");
}
