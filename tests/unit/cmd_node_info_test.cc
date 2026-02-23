#include "cmd_node_info.hh"
#include "hash.hh"
#include "router.hh"
#include <seastar/testing/test_case.hh>
#include <seastar/util/memory-data-sink.hh>

using namespace seastar;
using namespace shunyakv;
namespace shunyakv {
using OutBuf = std::vector<seastar::temporary_buffer<char>>;
using OutputStream = seastar::output_stream<char>;
} // namespace shunyakv

std::string get_resp_eq(std::string);

struct TestOut {
    OutBuf bufs;
    OutputStream out;

    TestOut()
        : bufs(),
          out(seastar::data_sink(
              std::make_unique<seastar::util::memory_data_sink>(bufs))) {}
};

static inline TestOut make_out() { return {}; }

static inline seastar::sstring bufs_to_sstring(OutBuf &bufs) {

    seastar::sstring s;
    for (auto &b : bufs) {
        s.append(b.get(), b.size());
    }
    return s;
}

/**
 * TEST NODE_INFO MAP
 */
SEASTAR_TEST_CASE(NODE_INFO_MAP_TEST) {
    const node_cfg cfg{
        .base_port = 60110,
        .smp = 4,
        .port_offset = 1,
        .hash = "fnv1a435345",
        .epoch = 7,
        .isPreHashed = false,
    };
    set_node_cfg(cfg);

    const resp::ArgvView cmd{"NODE_INFO", "MAP"};
    auto o = make_out();
    co_await handle_node_info(cmd, o.out, local_service());
    co_await o.out.close();

    const auto json = compute_hash_ranges();
    const auto expected = get_resp_eq(json);
    const auto reply = bufs_to_sstring(o.bufs);
    BOOST_REQUIRE_EQUAL(reply, expected);
}

/**
 * TEST NODE_INFO
 */
SEASTAR_TEST_CASE(NODE_INFO_CURRENT_CONN_TEST) {
    auto sid = this_shard_id();
    const auto json = compute_hash(sid);
    const resp::ArgvView cmd{"NODE_INFO"};
    auto o = make_out();
    co_await handle_node_info(cmd, o.out, local_service());
    co_await o.out.close();

    const auto expected = get_resp_eq(json);
    const auto reply = bufs_to_sstring(o.bufs);
    BOOST_REQUIRE_EQUAL(reply, expected);
}

/**
 * TEST NODE_INFO <SHARD_ID>
 */
SEASTAR_TEST_CASE(NODE_INFO_OTHER_NODE_TEST) {
    auto currentSid = this_shard_id();
    auto total = smp::count;
    auto sid = (currentSid + 1) % total;
    const auto json = compute_hash(sid);
    const resp::ArgvView cmd{"NODE_INFO", std::to_string(sid)};
    auto o = make_out();
    co_await handle_node_info(cmd, o.out, local_service());
    co_await o.out.close();

    const auto expected = get_resp_eq(json);
    const auto reply = bufs_to_sstring(o.bufs);
    BOOST_REQUIRE_EQUAL(reply, expected);
}

/**
 * Helper functions
 */

std::string get_resp_eq(std::string value) {
    std::string resp_value_string =
        "$" + std::to_string(value.length()) + "\r\n" + value + "\r\n";
    return resp_value_string;
}
