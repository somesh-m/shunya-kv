#include "cmd_get.hh"
#include "commands.hh"
#include "router.hh" // shard_for(), service

#include <resp/resp_types.hh>
#include <resp/resp_writer.hh>

#include <hash.hh>
#include <optional>
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <seastar/util/log.hh>
#include <string>

static seastar::logger get_logger{"cmd_get"};

namespace shunyakv {

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

    // Check shard
    const unsigned sid = shard_for(std::string_view(key.data(), key.size()));
    if (sid != seastar::this_shard_id()) {
        get_logger.info("WRONGSHARD: get {} on shard {} expected {}", key,
                        seastar::this_shard_id(), sid);
        co_await resp::write_error(out, "WRONGSHARD");
        co_return;
    }

    // Fetch from owner shard (local)
    std::optional<std::string> val =
        co_await store.local_get(std::string(key.data(), key.size()));

    if (val) {
        // RESP bulk string reply
        co_await resp::write_bulk(out,
                                  seastar::sstring(val->data(), val->size()));
        // (or if you change service to return sstring, you can avoid this copy)
    } else {
        // RESP null bulk for misses (redis-cli expects this)
        co_await resp::write_null(out);
        // Optional debug:
        // get_logger.debug("NOT_FOUND {}", key);
    }

    co_return;
}

} // namespace shunyakv
