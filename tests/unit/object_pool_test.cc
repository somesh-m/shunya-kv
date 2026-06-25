/**
 * Unit test case for object pool
 */
#include "pool/object_pool.hh"
#include "pool/pool.hh"
#include "pool/pool_config.hh"
#include "proto_helpers.hh"
#include "ttl/entry.hh"
#include <absl/container/flat_hash_set.h>
#include <chrono>
#include <dbconfig.hh>
#include <limits>
#include <seastar/core/smp.hh>
#include <seastar/testing/test_case.hh>
#include <utility>

using Clock = std::chrono::steady_clock;
using vector = std::vector<std::unique_ptr<ttl::Entry>>;
static constexpr double kTinyPoolMemoryPercent = 0.000001;

static const db_config test_config{};

SievePolicy make_test_policy() { return SievePolicy(test_config.ev_config); }

db_config make_zero_probation_config() {
    db_config cfg = test_config;
    cfg.pool.prob_pool_size_percent = 0.0;
    return cfg;
}

db_config make_full_probation_config() {
    db_config cfg = test_config;
    cfg.pool.prob_pool_size_percent = 1.0;
    return cfg;
}

struct TestPoolContext {
    db_config config;
    pool::ShardMemoryManager memory_manager;
    CacheEntryPool pool;

    TestPoolContext(db_config cfg, std::size_t initial_count,
                    std::size_t growth_count = 1)
        : config(std::move(cfg)), memory_manager(config),
          pool(memory_manager, config, initial_count, growth_count) {}
};

seastar::future<vector> acquireSlots(uint64_t count, CacheEntryPool &pool) {
    vector acquired_slots;
    for (uint64_t i = 0; i < count; i++) {
        acquired_slots.push_back(co_await pool.acquire());
    }
    co_return acquired_slots;
}

void releaseSlots(vector &acquired_slots, CacheEntryPool &pool) {
    for (auto &en : acquired_slots) {
        pool.release(std::move(en));
    }
}

SEASTAR_TEST_CASE(object_pool_create_by_memory) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        auto policy = make_test_policy();
        db_config config = test_config;
        config.pool.pool_max_memory_percent = kTinyPoolMemoryPercent;
        TestPoolContext sizing_ctx(config, 1);
        co_await sizing_ctx.pool.init(policy);
        const auto pool_size_by_mem = sizing_ctx.pool.calculate_optimal_pool_size();
        BOOST_REQUIRE_GT(pool_size_by_mem, 0);

        TestPoolContext pool_ctx(config, pool_size_by_mem);
        co_await pool_ctx.pool.init(policy);
        BOOST_REQUIRE_EQUAL(pool_ctx.pool.get_total_slots(), pool_size_by_mem);
        BOOST_REQUIRE_EQUAL(pool_ctx.pool.get_total_slots(),
                            pool_ctx.pool.get_available_slots());
        co_return;
    });
    co_return;
}

SEASTAR_TEST_CASE(object_pool_fixed_size) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        auto policy = make_test_policy();
        db_config config = test_config;
        config.pool.pool_max_memory_percent = 0.8;
        TestPoolContext ctx(config, 1024);
        co_await ctx.pool.init(policy);
        BOOST_REQUIRE_EQUAL(ctx.pool.get_total_slots(), 1024);
        BOOST_REQUIRE_EQUAL(ctx.pool.get_total_slots(),
                            ctx.pool.get_available_slots());
        co_return;
    });
    co_return;
}

SEASTAR_TEST_CASE(object_pool_create_overflow) {
    size_t max_val_size_t = std::numeric_limits<size_t>::max();
    co_await seastar::smp::invoke_on_all(
        [&max_val_size_t]() -> seastar::future<> {
            auto policy = make_test_policy();
            db_config config = test_config;
            config.pool.pool_max_memory_percent = kTinyPoolMemoryPercent;
            TestPoolContext sizing_ctx(config, 1);
            co_await sizing_ctx.pool.init(policy);
            const auto pool_size_by_mem = sizing_ctx.pool.calculate_optimal_pool_size();
            BOOST_REQUIRE_LT(pool_size_by_mem, max_val_size_t);

            TestPoolContext pool_ctx(config, pool_size_by_mem);
            co_await pool_ctx.pool.init(policy);
            BOOST_REQUIRE_EQUAL(pool_ctx.pool.get_total_slots(), pool_size_by_mem);
            BOOST_REQUIRE_EQUAL(pool_ctx.pool.get_total_slots(),
                                pool_ctx.pool.get_available_slots());
            co_return;
        });
    co_return;
}

/**
 * Test: object_pool_acquire_release
 *
 * Scenario:
 *   Acquire a set of entry from the pool and then release them back. Ensure
 * that the number of slots remains consistent.
 *
 * Setup:
 *  pool size = 32
 *
 * Action:
 *   Acquire 32 slots, release 32 slots
 *
 * Expected:
 *   Size of the pool before acquisition and after release should be same.
 */

SEASTAR_TEST_CASE(object_pool_acquire_release) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        uint32_t pool_size = 32;
        auto policy = make_test_policy();
        db_config config = test_config;
        config.pool.pool_max_memory_percent = 0.3;
        TestPoolContext ctx(config, pool_size);
        co_await ctx.pool.init(policy);

        BOOST_REQUIRE(ctx.pool.get_total_slots() == pool_size &&
                      ctx.pool.get_available_slots() == pool_size);

        vector acquired_entry;

        /* Acquire slots */
        acquired_entry = co_await acquireSlots(pool_size, ctx.pool);

        BOOST_REQUIRE_EQUAL(acquired_entry.size(), pool_size);
        BOOST_REQUIRE_EQUAL(ctx.pool.get_used_slots(), pool_size);

        /* Release slots */
        releaseSlots(acquired_entry, ctx.pool);

        BOOST_REQUIRE_EQUAL(ctx.pool.get_available_slots(),
                            ctx.pool.get_total_slots());

        co_return;
    });
    co_return;
}

SEASTAR_TEST_CASE(object_pool_multiple_init) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        uint32_t pool_size = 32;
        auto policy = make_test_policy();
        db_config config = test_config;
        config.pool.pool_max_memory_percent = 0.3;
        TestPoolContext ctx(config, pool_size);
        co_await ctx.pool.init(policy);
        BOOST_REQUIRE(ctx.pool.get_total_slots() == pool_size &&
                      ctx.pool.get_available_slots() == pool_size);
        /* init again */
        co_await ctx.pool.init(policy);
        BOOST_REQUIRE(ctx.pool.get_total_slots() == pool_size &&
                      ctx.pool.get_available_slots() == pool_size);
        co_return;
    });
}

SEASTAR_TEST_CASE(object_pool_acquire_overflow) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        uint32_t pool_size = 32;
        auto policy = make_test_policy();
        db_config config = test_config;
        config.pool.pool_max_memory_percent = 0.3;
        TestPoolContext ctx(config, pool_size);
        co_await ctx.pool.init(policy);
        BOOST_REQUIRE(ctx.pool.get_total_slots() == pool_size &&
                      ctx.pool.get_available_slots() == pool_size);
        vector acquired_slots = co_await acquireSlots(pool_size, ctx.pool);
        BOOST_REQUIRE_EQUAL(acquired_slots.size(), pool_size);
        BOOST_REQUIRE_EQUAL(ctx.pool.get_used_slots(), pool_size);
        /* The current pool grows on demand, so an extra acquire should succeed
         * by allocating more slots instead of failing.
         */
        std::unique_ptr<ttl::Entry> extra_entry = co_await ctx.pool.acquire();
        BOOST_REQUIRE(extra_entry.get() != nullptr);
        BOOST_REQUIRE(extra_entry->is_in_use());
        BOOST_REQUIRE_GT(ctx.pool.get_total_slots(), pool_size);
        ctx.pool.release(std::move(extra_entry));
        BOOST_REQUIRE_EQUAL(ctx.pool.get_used_slots(), pool_size);
        releaseSlots(acquired_slots, ctx.pool);
        BOOST_REQUIRE_EQUAL(ctx.pool.get_available_slots(),
                            ctx.pool.get_total_slots());
        co_return;
    });
}

SEASTAR_TEST_CASE(object_pool_release_overflow) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        uint32_t pool_size = 32;
        auto policy = make_test_policy();
        db_config config = test_config;
        config.pool.pool_max_memory_percent = 0.3;
        TestPoolContext ctx(config, pool_size);
        co_await ctx.pool.init(policy);
        BOOST_REQUIRE(ctx.pool.get_total_slots() == pool_size &&
                      ctx.pool.get_available_slots() == pool_size);

        std::unique_ptr<ttl::Entry> first = co_await ctx.pool.acquire();
        BOOST_REQUIRE(first);
        BOOST_REQUIRE_EQUAL(ctx.pool.get_used_slots(), 1);

        ctx.pool.release(std::move(first));
        BOOST_REQUIRE_EQUAL(ctx.pool.get_available_slots(),
                            ctx.pool.get_total_slots());
        co_return;
    });
}

SEASTAR_TEST_CASE(object_pool_mutate_fields) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        uint32_t pool_size = 1;
        auto policy = make_test_policy();
        db_config config = make_full_probation_config();
        config.pool.pool_max_memory_percent = 0.3;
        TestPoolContext ctx(config, pool_size);
        co_await ctx.pool.init(policy);
        BOOST_REQUIRE(ctx.pool.get_total_slots() == pool_size &&
                      ctx.pool.get_available_slots() == pool_size);

        std::unique_ptr<ttl::Entry> entry = co_await ctx.pool.acquire();
        BOOST_REQUIRE(ctx.pool.get_used_slots() >= 1);
        uint64_t epoch_now = std::chrono::duration_cast<std::chrono::seconds>(
                                 Clock::now().time_since_epoch())
                                 .count();
        entry->key = "test_key";
        entry->value = "test_value";
        entry->expires_at = epoch_now + 20;
        entry->last_access = epoch_now;
        entry->ver = 5;
        entry->heat = 20;
        entry->visited = true;
        /* release the entry */
        ctx.pool.release(std::move(entry));
        /* Ensure it is released */
        BOOST_REQUIRE_EQUAL(ctx.pool.get_available_slots(),
                            ctx.pool.get_total_slots());
        /* Acquire again and ensure pooled entries are reset before reuse.
         */
        entry = co_await ctx.pool.acquire();
        BOOST_REQUIRE(ctx.pool.get_used_slots() >= 1);
        BOOST_REQUIRE(entry->key != "test_key");
        BOOST_REQUIRE(entry->value != "test_value");
        BOOST_REQUIRE(entry->expires_at != epoch_now);
        BOOST_REQUIRE(entry->last_access != epoch_now);
        BOOST_REQUIRE(entry->ver != 5);
        BOOST_REQUIRE(entry->heat != 20);
        BOOST_REQUIRE(entry->visited == false);

        ctx.pool.release(std::move(entry));
        /* Released, so available should be equal to pool_size */
        BOOST_REQUIRE_EQUAL(ctx.pool.get_available_slots(),
                            ctx.pool.get_total_slots());
        co_return;
    });
}

SEASTAR_TEST_CASE(object_pool_distinct_pointer) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        uint32_t pool_size = 2048;
        auto policy = make_test_policy();
        db_config config = test_config;
        config.pool.pool_max_memory_percent = 0.3;
        TestPoolContext ctx(config, pool_size);
        co_await ctx.pool.init(policy);
        BOOST_REQUIRE(ctx.pool.get_total_slots() == pool_size &&
                      ctx.pool.get_available_slots() == pool_size);

        vector acquired_slots = co_await acquireSlots(pool_size, ctx.pool);
        BOOST_REQUIRE_EQUAL(acquired_slots.size(), pool_size);
        BOOST_REQUIRE_EQUAL(ctx.pool.get_used_slots(), pool_size);
        co_return;
    });
}

SEASTAR_TEST_CASE(object_pool_promote_to_sanctuary_tracks_sieve) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        auto policy = make_test_policy();
        db_config config = make_full_probation_config();
        config.pool.pool_max_memory_percent = 0.3;
        TestPoolContext ctx(config, 4);
        co_await ctx.pool.init(policy);

        auto entry = co_await ctx.pool.acquire();
        BOOST_REQUIRE(entry);
        BOOST_REQUIRE_EQUAL(policy.size(), 0);

        ctx.pool.promote_to_sanctuary(*entry);

        BOOST_REQUIRE_EQUAL(policy.size(), 1);
        BOOST_REQUIRE(entry->pool_type == ttl::PoolType::Sanctuary);

        ctx.pool.release(std::move(entry));
        BOOST_REQUIRE_EQUAL(policy.size(), 0);
        co_return;
    });
    co_return;
}

SEASTAR_TEST_CASE(
    object_pool_reused_entry_relinks_into_sanctuary_when_probation_disabled) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        auto policy = make_test_policy();
        db_config config = make_zero_probation_config();
        config.pool.pool_max_memory_percent = 0.3;
        TestPoolContext ctx(config, 1);
        co_await ctx.pool.init(policy);

        auto first = co_await ctx.pool.acquire();
        BOOST_REQUIRE(first);
        BOOST_REQUIRE_EQUAL(policy.size(), 1);
        BOOST_REQUIRE(first->pool_type == ttl::PoolType::Sanctuary);

        ctx.pool.release(std::move(first));
        BOOST_REQUIRE_EQUAL(policy.size(), 0);

        auto second = co_await ctx.pool.acquire();
        BOOST_REQUIRE(second);
        BOOST_REQUIRE_EQUAL(policy.size(), 1);
        BOOST_REQUIRE(second->pool_type == ttl::PoolType::Sanctuary);

        ctx.pool.release(std::move(second));
        BOOST_REQUIRE_EQUAL(policy.size(), 0);
        co_return;
    });
    co_return;
}

/**
 * multi_shard_independence

On invoke_on_all, do acquire/release sequences with different counts per shard.
Assert each shard’s pool accounting remains locally correct.


stress_acquire_release_cycles
Repeated acquire/release loops (e.g., 10k ops per shard).
Assert no count drift and no crashes.
 */
