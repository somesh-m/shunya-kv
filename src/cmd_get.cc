#include "cmd_get.hh"
#include "commands.hh"
#include "proto_helpers.hh" // split_first, trim
#include "router.hh"        // shard_for(), service

#include <hash.hh>
#include <optional>
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <seastar/util/log.hh>
#include <string>
#include <string_view>

static seastar::logger get_logger{"cmd_get"};
namespace shunyakv {

seastar::future<> handle_get(std::string_view args,
                             seastar::output_stream<char> &out,
                             shunyakv::service &store) {
    using shunyakv::proto::split_first;
    using shunyakv::proto::trim;

    // Parse: GET <key>
    auto [key_sv, _rest] = split_first(args);
    key_sv = trim(key_sv);

    if (key_sv.empty()) {
        co_await out.write("CLIENT_ERROR missing key\r\n");
        co_return;
    }

    // check if correct shard
    const unsigned sid = shard_for(key_sv);
    if (sid != seastar::this_shard_id()) {
        get_logger.info(
            "WRONGSHARD: Trying to get {} on shard {}. Expected Shard {} \n",
            key_sv, seastar::this_shard_id(), sid);
        co_await out.write("WRONGSHARD\r\n"); // or MOVED-CORE like below
        co_return;
    }

    // Cross-shard safe: return std::string from the owner shard
    std::optional<std::string> val =
        co_await store.local_get(std::string(key_sv));

    if (val) {
        co_await out.write(val->data(), val->size());
        co_await out.write("\r\n");
        get_logger.info("READ {}", key_sv);
    } else {
        get_logger.info("NOT_FOUND. Trying to fetch {} from shard {}", key_sv,
                        seastar::this_shard_id());
        co_await out.write("NOT_FOUND\r\n");
    }

    co_return;
}

} // namespace shunyakv
