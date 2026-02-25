#include "cmd_set.hh"
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
future<std::optional<sstring>> perform_local_get(std::string);

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

SEASTAR_TEST_CASE(SET_TEST_SET_VALID) {
    const resp::ArgvView cmd{"set", "name", "test"};

    auto o = make_out();
    co_await handle_set(cmd, o.out, local_service());
    co_await o.out.close();

    const auto sid = shard_for("name");
    auto value = (sid == this_shard_id())
                     ? co_await local_service().local_get("name")
                     : co_await smp::submit_to(sid, [] {
                           return local_service().local_get("name");
                       });
    BOOST_REQUIRE(value.has_value());
    BOOST_REQUIRE_EQUAL(*value, "test");
}

SEASTAR_TEST_CASE(SET_TEST_INVALID_ARGS) {
    const resp::ArgvView cmd{"set", "name"};
    shunyakv::service _service = local_service();
    auto o = make_out();
    co_await handle_set(cmd, o.out, _service);
    co_await o.out.close();
    auto reply = bufs_to_sstring(o.bufs);
    auto value = co_await _service.local_get("name");
    BOOST_REQUIRE(!value.has_value());
    BOOST_REQUIRE_EQUAL(reply, "-ERR wrong number of arguments for 'SET'\r\n");
}

SEASTAR_TEST_CASE(SET_TEST_SHARD_HOPPING) {
    std::string key;
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
    const resp::ArgvView cmd{"set", key, "test"};

    auto o = make_out();
    co_await handle_set(cmd, o.out, local_service());
    co_await o.out.close();
    std::string reply = bufs_to_sstring(o.bufs);
    auto value = co_await smp::submit_to(sid, [key = seastar::sstring(key)] {
        return local_service().local_get(key);
    });
    BOOST_REQUIRE(value.has_value());
    BOOST_REQUIRE_EQUAL(*value, "test");
    BOOST_REQUIRE_EQUAL(reply, "+OK\r\n");
}

SEASTAR_TEST_CASE(SET_TEST_OVERWRITE_EXISTING) {
    const resp::ArgvView cmd_a = {"set", "name", "test_a"};
    const resp::ArgvView cmd_b = {"set", "name", "test_b"};
    auto &svc = local_service();
    auto o1 = make_out();
    co_await handle_set(cmd_a, o1.out, svc);
    co_await o1.out.close();
    // Ensure first write went through
    auto reply = bufs_to_sstring(o1.bufs);
    auto value = co_await perform_local_get("name");
    BOOST_REQUIRE(value.has_value());
    BOOST_REQUIRE_EQUAL(*value, "test_a");
    BOOST_REQUIRE_EQUAL(reply, "+OK\r\n");

    // Overwrite
    auto o2 = make_out();
    co_await handle_set(cmd_b, o2.out, svc);
    co_await o2.out.close();
    // Ensure second write went through
    reply = bufs_to_sstring(o2.bufs);
    value = co_await perform_local_get("name");
    BOOST_REQUIRE(value.has_value());
    BOOST_REQUIRE_EQUAL(*value, "test_b");
    BOOST_REQUIRE_EQUAL(reply, "+OK\r\n");
}

SEASTAR_TEST_CASE(SET_TEST_TTL_VALID) {
    const resp::ArgvView cmd{"set", "name", "test", "ex", "5"};

    auto o = make_out();
    co_await handle_set(cmd, o.out, local_service());
    co_await o.out.close();

    const auto sid = shard_for("name");
    auto value = (sid == this_shard_id())
                     ? co_await local_service().local_get("name")
                     : co_await smp::submit_to(sid, [] {
                           return local_service().local_get("name");
                       });
    BOOST_REQUIRE(value.has_value());
    BOOST_REQUIRE_EQUAL(*value, "test");

    co_await sleep(std::chrono::seconds(6));
    value = (sid == this_shard_id())
                ? co_await local_service().local_get("name")
                : co_await smp::submit_to(
                      sid, [] { return local_service().local_get("name"); });
    BOOST_REQUIRE(!value.has_value());
}

SEASTAR_TEST_CASE(SET_TEST_TTL_NON_NUMERIC_ARGS) {
    const resp::ArgvView cmd{"set", "name", "test", "ex", "five"};

    auto o = make_out();
    co_await handle_set(cmd, o.out, local_service());
    co_await o.out.close();
    std::string reply = bufs_to_sstring(o.bufs);

    BOOST_REQUIRE_EQUAL(reply, "-ERR invalid expire time\r\n");
}

/**
 *
 */
SEASTAR_TEST_CASE(SET_TEST_TTL_REFRESH_ON_OVERWRITE) {
    const resp::ArgvView cmd{"set", "name", "test", "ex", "5"};
    const resp::ArgvView cmd_re{"set", "name", "test_2", "ex", "10"};

    auto o = make_out();
    co_await handle_set(cmd, o.out, local_service());
    co_await o.out.close();

    const auto sid = shard_for("name");
    auto value = (sid == this_shard_id())
                     ? co_await local_service().local_get("name")
                     : co_await smp::submit_to(sid, [] {
                           return local_service().local_get("name");
                       });
    BOOST_REQUIRE(value.has_value());
    BOOST_REQUIRE_EQUAL(*value, "test");

    auto o1 = make_out();
    co_await handle_set(cmd_re, o.out, local_service());
    co_await o.out.close();

    value = (sid == this_shard_id())
                ? co_await local_service().local_get("name")
                : co_await smp::submit_to(
                      sid, [] { return local_service().local_get("name"); });
    BOOST_REQUIRE(value.has_value());
    BOOST_REQUIRE_EQUAL(*value, "test_2");

    co_await sleep(std::chrono::seconds(6));
    value = (sid == this_shard_id())
                ? co_await local_service().local_get("name")
                : co_await smp::submit_to(
                      sid, [] { return local_service().local_get("name"); });
    BOOST_REQUIRE(value.has_value());

    co_await sleep(std::chrono::seconds(11));
    value = (sid == this_shard_id())
                ? co_await local_service().local_get("name")
                : co_await smp::submit_to(
                      sid, [] { return local_service().local_get("name"); });
    BOOST_REQUIRE(!value.has_value());
}

SEASTAR_TEST_CASE(SET_TEST_TTL_SET_WITHOUT_TTL_OVERWRITE) {
    const resp::ArgvView cmd{"set", "name", "test", "ex", "6"};
    const resp::ArgvView cmd_re{"set", "name", "test_2"};

    auto o = make_out();
    co_await handle_set(cmd, o.out, local_service());
    co_await o.out.close();

    const auto sid = shard_for("name");
    auto value = (sid == this_shard_id())
                     ? co_await local_service().local_get("name")
                     : co_await smp::submit_to(sid, [] {
                           return local_service().local_get("name");
                       });
    BOOST_REQUIRE(value.has_value());
    BOOST_REQUIRE_EQUAL(*value, "test");

    auto o1 = make_out();
    co_await handle_set(cmd_re, o.out, local_service());
    co_await o.out.close();

    value = (sid == this_shard_id())
                ? co_await local_service().local_get("name")
                : co_await smp::submit_to(
                      sid, [] { return local_service().local_get("name"); });
    BOOST_REQUIRE(value.has_value());
    BOOST_REQUIRE_EQUAL(*value, "test_2");

    co_await sleep(std::chrono::seconds(7));
    value = (sid == this_shard_id())
                ? co_await local_service().local_get("name")
                : co_await smp::submit_to(
                      sid, [] { return local_service().local_get("name"); });
    BOOST_REQUIRE(!value.has_value());
}

SEASTAR_TEST_CASE(SET_TEST_EMPTY_VALUE) {
    const resp::ArgvView cmd{"set", "name", ""};
    shunyakv::service _service = local_service();
    auto o = make_out();
    co_await handle_set(cmd, o.out, _service);
    co_await o.out.close();
    auto reply = bufs_to_sstring(o.bufs);
    auto value = co_await perform_local_get("name");
    BOOST_REQUIRE(value.has_value());
    BOOST_REQUIRE_EQUAL(*value, "");
    BOOST_REQUIRE_EQUAL(reply, "+OK\r\n");
}

SEASTAR_TEST_CASE(SET_TEST_LARGE_VALUE) {
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
    auto &svc = local_service();
    for (unsigned i = 0; i < sizeVector.size(); i++) {
        const auto sz = sizeVector[i];

        // fresh output capture per iteration (important)
        auto o = make_out();

        // generate value of exactly sz bytes
        std::string value = get_value(sz);

        // Keep key small/unique; this test stresses large values.
        std::string key = "name_" + std::to_string(sz);

        const resp::ArgvView cmd{"set", key, value};

        co_await handle_set(cmd, o.out, svc);
        co_await o.out.close();

        auto reply = bufs_to_sstring(o.bufs);

        auto storedVal = co_await perform_local_get(key);
        BOOST_REQUIRE(storedVal.has_value());
        BOOST_REQUIRE_EQUAL(*storedVal, value);
        BOOST_REQUIRE_EQUAL(reply, "+OK\r\n");
    }
}

SEASTAR_TEST_CASE(SET_TEST_LARGE_KEY) {
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
    auto &svc = local_service();
    for (unsigned i = 0; i < sizeVector.size(); i++) {
        const auto sz = sizeVector[i];

        // fresh output capture per iteration (important)
        auto o = make_out();

        // generate value of exactly sz bytes
        std::string key = get_value(sz);

        // Keep key small/unique; this test stresses large values.
        std::string value = "test_" + std::to_string(sz);

        const resp::ArgvView cmd{"set", key, value};

        co_await handle_set(cmd, o.out, svc);
        co_await o.out.close();

        auto reply = bufs_to_sstring(o.bufs);

        auto storedVal = co_await perform_local_get(key);
        BOOST_REQUIRE(storedVal.has_value());
        BOOST_REQUIRE_EQUAL(*storedVal, value);
        BOOST_REQUIRE_EQUAL(reply, "+OK\r\n");
    }
}

/**
 * Helper functions
 */

std::string get_value(uint64_t size) { return std::string(size, 'a'); }

future<std::optional<sstring>> perform_local_get(std::string key) {
    const auto sid = shard_for(key);
    if (sid == this_shard_id()) {
        co_return co_await local_service().local_get(key);
    }
    co_return co_await smp::submit_to(sid,
                                      [k = seastar::sstring(std::move(key))] {
                                          return local_service().local_get(k);
                                      });
}
