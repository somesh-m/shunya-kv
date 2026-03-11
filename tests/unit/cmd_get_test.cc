#include "cmd_get.hh"
#include "hash.hh"
#include "router.hh"
#include <seastar/core/sleep.hh>
#include <seastar/testing/test_case.hh>
#include <seastar/util/memory-data-sink.hh>

using namespace seastar;
using namespace shunyakv;
namespace shunyakv {
using OutBuf = std::vector<seastar::temporary_buffer<char>>;
using OutputStream = seastar::output_stream<char>;
} // namespace shunyakv

std::string get_value(uint64_t);
future<bool> perform_local_set(std::string, std::string);
future<bool> perform_local_set(std::string, std::string,
                               std::optional<uint32_t> ttl_sec);
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

SEASTAR_TEST_CASE(GET_TEST_VALID) {
    std::string key = "name";
    std::string expected_value = "test";
    std::string resp_value_string = get_resp_eq(expected_value);
    const resp::ArgvView cmd{"get", key};

    auto o = make_out();
    // insert the key value first
    co_await perform_local_set(key, expected_value);

    co_await handle_get(cmd, o.out, local_service());
    co_await o.out.close();

    auto reply = bufs_to_sstring(o.bufs);
    BOOST_REQUIRE_EQUAL(reply, resp_value_string);
}

SEASTAR_TEST_CASE(GET_TEST_INVALID_ARGS) {
    const resp::ArgvView cmd{"GET", "NAME", "TEST"};
    shunyakv::service &svc = local_service();
    auto o = make_out();
    co_await handle_get(cmd, o.out, svc);
    co_await o.out.close();
    auto reply = bufs_to_sstring(o.bufs);
    BOOST_REQUIRE_EQUAL(reply, "-ERR wrong number of arguments for 'GET'\r\n");
}

SEASTAR_TEST_CASE(GET_TEST_INVALID_KEY) {
    std::string key = "non_existent_key";
    std::string resp_value_string = "$-1\r\n";
    const resp::ArgvView cmd{"get", key};

    auto o = make_out();

    co_await handle_get(cmd, o.out, local_service());
    co_await o.out.close();

    auto reply = bufs_to_sstring(o.bufs);
    BOOST_REQUIRE_EQUAL(reply, resp_value_string);
}

SEASTAR_TEST_CASE(GET_TEST_SHARD_HOPPING) {
    std::string key;
    std::string value = "test";
    unsigned sid = this_shard_id();
    for (unsigned i = 0; i < 1024; ++i) {
        auto candidate = "hop_key_" + std::to_string(i);
        auto candidate_sid = shard_for(candidate);
        // If candidate shard id and current shard id is same, modify the key
        if (candidate_sid != this_shard_id()) {
            key = std::move(candidate);
            sid = candidate_sid;
            break;
        }
    }
    BOOST_REQUIRE(!key.empty());
    const resp::ArgvView cmd{"get", key};
    co_await perform_local_set(key, value);

    auto o = make_out();
    co_await handle_get(cmd, o.out, local_service());
    co_await o.out.close();
    std::string reply = bufs_to_sstring(o.bufs);
    BOOST_REQUIRE_EQUAL(reply, get_resp_eq(value));
}

SEASTAR_TEST_CASE(GET_TEST_OVERWRITE_EXISTING) {
    std::string key = "name_overwrite_test";
    std::string value_a = "test_ab";
    std::string value_b = "test_bc";
    const resp ::ArgvView cmd{"get", key};
    co_await perform_local_set(key, value_a);
    auto o = make_out();
    co_await handle_get(cmd, o.out, local_service());
    co_await o.out.close();
    std::string reply = bufs_to_sstring(o.bufs);
    BOOST_TEST_MESSAGE("GET_TEST_OVERWRITE_EXISTING first reply: " << reply);
    BOOST_REQUIRE_EQUAL(reply, get_resp_eq(value_a));

    // overwrite the key
    co_await perform_local_set(key, value_b);
    auto o1 = make_out();
    co_await handle_get(cmd, o1.out, local_service());
    co_await o1.out.close();
    std::string reply1 = bufs_to_sstring(o1.bufs);
    BOOST_REQUIRE_EQUAL(reply1, get_resp_eq(value_b));
}

SEASTAR_TEST_CASE(GET_TEST_TTL_VALID) {
    std::string key = "name";
    std::string expected_value = "test";
    std::string resp_value_string = get_resp_eq(expected_value);
    const resp::ArgvView cmd{"get", key};

    auto o = make_out();
    // insert the key value first
    co_await perform_local_set(key, expected_value, 5);

    co_await handle_get(cmd, o.out, local_service());
    co_await o.out.close();

    auto reply = bufs_to_sstring(o.bufs);
    BOOST_REQUIRE_EQUAL(reply, resp_value_string);

    co_await sleep(std::chrono::seconds(7));

    auto o1 = make_out();
    co_await handle_get(cmd, o1.out, local_service());
    co_await o1.out.close();

    auto reply1 = bufs_to_sstring(o1.bufs);
    BOOST_REQUIRE_EQUAL(reply1, "$-1\r\n");
}

SEASTAR_TEST_CASE(GET_TEST_LARGE_VALUE) {
    std::vector<uint64_t> sizeVector;
    sizeVector.push_back(4096);        // 4KB
    sizeVector.push_back(8192);        // 8KB
    sizeVector.push_back(16384);       // 16KB
    sizeVector.push_back(32768);       // 32KB
    sizeVector.push_back(65536);       // 64KB
    sizeVector.push_back(131072);      // 128KB
    sizeVector.push_back(262144);      // 256KB
    sizeVector.push_back(524288);      // 512KB
    sizeVector.push_back(1048576);     // 1MB
    sizeVector.push_back(2 * 1048576); // 2MB
    sizeVector.push_back(3 * 1048576); // 3MB
    sizeVector.push_back(4 * 1048576); // 4MB
    sizeVector.push_back(5 * 1048576); // 5MB
    std::string key = "test";
    const resp::ArgvView cmd{"get", key};
    auto &svc = local_service();
    for (unsigned i = 0; i < sizeVector.size(); i++) {
        const auto sz = sizeVector[i];

        auto o = make_out();

        std::string value = get_value(sz);
        co_await perform_local_set(key, value);

        co_await handle_get(cmd, o.out, svc);
        co_await o.out.close();

        auto reply = bufs_to_sstring(o.bufs);

        BOOST_REQUIRE_EQUAL(reply, get_resp_eq(value));
    }
}

/**
 * Helper functions
 */
std::string get_resp_eq(std::string value) {
    std::string resp_value_string =
        "$" + std::to_string(value.length()) + "\r\n" + value + "\r\n";
    return resp_value_string;
}
std::string get_value(uint64_t size) { return std::string(size, 'a'); }

future<bool> perform_local_set(std::string key, std::string value) {
    return perform_local_set(std::move(key), std::move(value), std::nullopt);
}

future<bool> perform_local_set(std::string key, std::string value,
                               std::optional<uint32_t> ttl_sec) {
    const auto sid = shard_for(key);
    if (sid == this_shard_id()) {
        if (!ttl_sec)
            co_return co_await local_service().local_set(key, value);

        co_return co_await local_service().local_set(
            key, value, static_cast<uint64_t>(*ttl_sec));
    }
    co_return co_await smp::submit_to(
        sid,
        [k = seastar::sstring(std::move(key)),
         v = seastar::sstring(std::move(value)), ttl_sec = std::move(ttl_sec)] {
            if (!ttl_sec)
                return local_service().local_set(k, v);

            return local_service().local_set(k, v,
                                             static_cast<uint64_t>(*ttl_sec));
        });
}
