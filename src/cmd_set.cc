#include "cmd_set.hh"
#include "commands.hh"
#include "hash.hh"
#include "router.hh" // service
#include <resp/resp_types.hh>
#include <resp/resp_writer.hh>

#include <seastar/core/coroutine.hh>
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <string>

static seastar::logger set_logger{"cmd_set"};

namespace shunyakv {

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
        try {
            auto n = std::stoll(std::string(cmd[4].data(), cmd[4].size()));
            if (n <= 0) {
                co_await resp::write_error(out, "ERR invalid expire time");
                co_return;
            }
            ttl_ms = static_cast<uint64_t>(n);
        } catch (...) {
            resp::write_error(out, "ERR invalid expire time");
            co_return;
        }
    } else if (cmd.size() != 3) {
        co_await resp::write_error(out, "ERR syntax error");
        co_return;
    }

    // Check shard
    unsigned sid =
        shunyakv::shard_for(std::string_view(key.data(), key.size()));
    if (sid != seastar::this_shard_id()) {
        set_logger.info("WRONGSHARD: set {} on shard {} expected {}", key,
                        seastar::this_shard_id(), sid);
        co_await resp::write_error(out, "WRONGSHARD");
        co_return;
    }

    // Store set: update your service API to accept ttl_ms if you want TTL
    bool ok = false;
    if (ttl_ms == 0) {
        ok = co_await store.local_set(std::string(key.data(), key.size()),
                                      std::string(value.data(), value.size()));
    } else {
        // You likely want to add a local_set overload:
        // local_set(key, value, ttl_ms)
        ok = co_await store.local_set(std::string(key.data(), key.size()),
                                      std::string(value.data(), value.size()),
                                      ttl_ms);
    }

    if (ok) {
        co_await resp::write_simple(out, "OK");
    } else {
        co_await resp::write_error(out, "NOT STORED");
        set_logger.info("NOT STORED {}", key);
    }

    co_return;
}

} // namespace shunyakv
