#include "cmd_get.hh"
#include "commands.hh"
#include "router.hh" // shard_for(), service

#include <resp/resp_types.hh>
#include <resp/resp_writer.hh>

#include "hotpath_metrics.hh"
#include <chrono>
#include <hash.hh>
#include <optional>
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <seastar/core/smp.hh>
#include <seastar/util/log.hh>
#include <string>

static seastar::logger vget_logger{"cmd_vget"};

namespace shunyakv {
namespace {

seastar::future<std::optional<seastar::sstring>>
get_key_value(shunyakv::service &store, std::string_view key,
              std::string_view index) {
    return store.vget(key, index);
}

} // namespace

seastar::future<> handle_vget(const resp::ArgvView &cmd,
                              seastar::output_stream<char> &out,
                              shunyakv::service &store) {
    // cmd[0] == "GET"
    if (cmd.size() < 3) {
        co_await resp::write_error(out,
                                   "ERR wrong number of arguments for 'VGET'");
        co_return;
    }

    const auto &key = cmd[1];
    if (key.empty()) {
        co_await resp::write_error(out, "ERR empty key");
        co_return;
    }

    const auto &index = cmd[2];
    if (index.empty()) {
        co_await resp::write_error(out, "INDEX empty key");
        co_return;
    }

    auto val = co_await get_key_value(store, key, index);

    if (val) {
        co_await resp::write_bulk(out, *val);
    } else {
        co_await resp::write_null(out);
    }
    co_return;
}

} // namespace shunyakv
