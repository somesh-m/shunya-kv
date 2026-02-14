#include "cmd_set.hh"
#include "commands.hh"
#include "hash.hh"
#include "router.hh" // service
#include <resp/resp_types.hh>
#include <resp/resp_writer.hh>

#include <seastar/core/coroutine.hh>
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/smp.hh>
#include <charconv>
#include <chrono>
#include <string>

static seastar::logger set_logger{"cmd_set"};

namespace shunyakv {
namespace {

seastar::future<bool> set_key_value(shunyakv::service &store,
                                    std::string_view key,
                                    seastar::sstring value, uint64_t ttl_ms) {
    if (ttl_ms == 0) {
        return store.local_set(std::move(key), std::move(value));
    }
    return store.local_set(std::move(key), std::move(value), ttl_ms);
}

} // namespace

static bool ieq(const seastar::sstring &a, const char *b) {
    // simple ASCII case-insensitive compare for tiny tokens like "EX"
    if (a.size() != std::strlen(b))
        return false;
    for (size_t i = 0; i < a.size(); i++) {
        char ca = a[i];
        char cb = b[i];
        if ('a' <= ca && ca <= 'z')
            ca = char(ca - 'a' + 'A');
        if ('a' <= cb && cb <= 'z')
            cb = char(cb - 'a' + 'A');
        if (ca != cb)
            return false;
    }
    return true;
}

seastar::future<> handle_set(const resp::Array &cmd,
                             seastar::output_stream<char> &out,
                             shunyakv::service &store) {
    // cmd[0] == "SET"
    if (cmd.size() < 3) {
        co_await resp::write_error(out,
                                   "ERR wrong number of arguments for 'SET'");
        co_return;
    }

    const auto &key = cmd[1];
    const auto &value = cmd[2];

    if (key.empty()) {
        co_await resp::write_error(out, "ERR empty key");
        co_return;
    }

    // Optional: SET key value EX 5000
    uint64_t ttl_ms = 0;
    if (cmd.size() == 5) {
        if (!ieq(cmd[3], "EX")) {
            co_await resp::write_error(out, "ERR syntax error (expected EX)");
            co_return;
        }
        uint64_t n = 0;
        const auto [ptr, ec] =
            std::from_chars(cmd[4].data(), cmd[4].data() + cmd[4].size(), n);
        if (ec != std::errc{} || ptr != cmd[4].data() + cmd[4].size() ||
            n == 0) {
            co_await resp::write_error(out, "ERR invalid expire time");
            co_return;
        }
        ttl_ms = n;
    } else if (cmd.size() != 3) {
        co_await resp::write_error(out, "ERR syntax error");
        co_return;
    }
    const auto start = std::chrono::steady_clock::now();

    // Check shard
    unsigned sid =
        shunyakv::shard_for(std::string_view(key.data(), key.size()));
    const bool forwarded = (sid != seastar::this_shard_id());
    store.record_set(forwarded);
    seastar::sstring value_str(value.data(), value.size());
    bool ok = false;
    if (!forwarded) {
        ok = co_await set_key_value(store, key,
                                    std::move(value_str), ttl_ms);
    } else {
        ok = co_await seastar::smp::submit_to(
            sid, [key = seastar::sstring(key), value = std::move(value_str),
                  ttl_ms]() mutable {
                return set_key_value(shunyakv::local_service(), key,
                                     std::move(value), ttl_ms);
            });
    }

    if (ok) {
        co_await resp::write_simple(out, "OK");
    } else {
        co_await resp::write_error(out, "NOT STORED");
        set_logger.info("NOT STORED {}", key);
    }
    const auto latency_us =
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::steady_clock::now() - start)
                                  .count());
    store.record_set_latency(forwarded, latency_us);

    co_return;
}

} // namespace shunyakv
