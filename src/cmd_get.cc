#include "cmd_get.hh"
#include "commands.hh"
#include "router.hh" // shard_for(), service

#include <resp/resp_types.hh>
#include <resp/resp_writer.hh>

#include <hash.hh>
#include <chrono>
#include <optional>
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/smp.hh>
#include <seastar/util/log.hh>
#include <string>

static seastar::logger get_logger{"cmd_get"};

namespace shunyakv {
namespace {

seastar::future<std::optional<seastar::sstring>>
get_key_value(shunyakv::service &store, std::string_view key) {
    return store.local_get(key);
}

} // namespace

seastar::future<> handle_get(const resp::Array &cmd,
                             seastar::output_stream<char> &out,
                             shunyakv::service &store) {
    // cmd[0] == "GET"
    if (cmd.size() != 2) {
        co_await resp::write_error(out,
                                   "ERR wrong number of arguments for 'GET'");
        co_return;
    }

    const auto &key = cmd[1];
    if (key.empty()) {
        co_await resp::write_error(out, "ERR empty key");
        co_return;
    }
#if SHUNYAKV_ENABLE_HOT_PATH_METRICS
    const auto start = std::chrono::steady_clock::now();
#endif

    // Check shard
    const unsigned sid = shard_for(std::string_view(key.data(), key.size()));
    const bool forwarded = (sid != seastar::this_shard_id());
    store.record_get(forwarded);
    std::optional<seastar::sstring> val;
    if (!forwarded) {
        val = co_await get_key_value(store, key);
    } else {
        get_logger.info("Forwarding GET key='{}' from shard {} to shard {}",
                        key, seastar::this_shard_id(), sid);
        val = co_await seastar::smp::submit_to(
            sid, [key = seastar::sstring(key)]() mutable {
                return get_key_value(shunyakv::local_service(), key);
            });
    }

    if (val) {
        // RESP bulk string reply
        co_await resp::write_bulk(out, *val);
        // (or if you change service to return sstring, you can avoid this copy)
    } else {
        // RESP null bulk for misses (redis-cli expects this)
        co_await resp::write_null(out);
        // Optional debug:
        // get_logger.debug("NOT_FOUND {}", key);
    }
#if SHUNYAKV_ENABLE_HOT_PATH_METRICS
    const auto latency_us =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::steady_clock::now() - start)
                                  .count());
    store.record_get_latency(latency_us);
#endif

    co_return;
}

} // namespace shunyakv
