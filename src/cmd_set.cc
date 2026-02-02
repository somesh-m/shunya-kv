#include "cmd_set.hh"
#include "commands.hh"
#include "proto_helpers.hh" // split_first, trim
#include "router.hh"        // shard_for(), service

#include <hash.hh>
#include <seastar/core/future.hh>
#include <seastar/core/iostream.hh>
#include <string>
#include <string_view>

static seastar::logger set_logger{"cmd_set"};
namespace shunyakv {
seastar::future<> handle_set(std::string_view args,
                             seastar::output_stream<char> &out,
                             shunyakv::service &store) {

    using shunyakv::proto::split_first;
    using shunyakv::proto::trim;

    // Parse: SET <key> <value...>
    auto [key_sv, rest_sv] = split_first(args);
    key_sv = trim(key_sv);
    rest_sv = trim(rest_sv);

    if (key_sv.empty() || rest_sv.empty()) {
        co_await out.write("CLIENT_ERROR missing key/value\r\n");
        co_return;
    }

    // check if correct shard
    unsigned sid = shard_for(key_sv);
    if (sid != seastar::this_shard_id()) {
        set_logger.info("SET WRONGSHARD \n");
        co_await out.write("WRONGSHARD\r\n"); // or MOVED-CORE like below
        co_return;
    }

    // const unsigned sid = shard_for(key_sv);

    // std::cout << "Sending request to shard " << sid << "\n";

    // Cross-shard safe: pass std::string over invoke_on
    // bool ok = co_await svc.invoke_on(
    //     sid, [key = std::string(key_sv),
    //           val = std::string(rest_sv)](service &s) mutable {
    //         return s.local_set(std::move(key), std::move(val));
    //     });

    bool ok =
        co_await store.local_set(std::string(key_sv), std::string(rest_sv));

    if (ok) {
        co_await out.write("STORED\r\n");
    } else {
        co_await out.write("NOT_STORED\r\n");
    }

    co_return;
}

} // namespace shunyakv
