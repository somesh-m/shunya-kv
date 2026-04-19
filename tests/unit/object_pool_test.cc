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

using Clock = std::chrono::steady_clock;
using vector = std::vector<std::unique_ptr<ttl::Entry>>;

static const db_config test_config{};

SievePolicy make_test_policy() { return SievePolicy(test_config.ev_config); }

db_config make_zero_probation_config() {
    db_config cfg = test_config;
    cfg.pool.prob_pool_size_percent = 0.0;
    return cfg;
}

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
        CacheEntryPool pool = CacheEntryPool(0);
        auto policy = make_test_policy();
        co_await pool.init(test_config, policy);
        uint64_t pool_size_by_mem = pool.calculate_optimal_pool_size();
        BOOST_REQUIRE_EQUAL(pool.get_total_slots(), pool_size_by_mem);
        BOOST_REQUIRE_EQUAL(pool.get_total_slots(), pool.get_available_slots());
        co_return;
    });
    co_return;
}

SEASTAR_TEST_CASE(object_pool_fixed_size) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        CacheEntryPool pool = CacheEntryPool(1024);
        auto policy = make_test_policy();
        co_await pool.init(test_config, policy);
        BOOST_REQUIRE_EQUAL(pool.get_total_slots(), 1024);
        BOOST_REQUIRE_EQUAL(pool.get_total_slots(), pool.get_available_slots());
        co_return;
    });
    co_return;
}

SEASTAR_TEST_CASE(object_pool_create_overflow) {
    size_t max_val_size_t = std::numeric_limits<size_t>::max();
    co_await seastar::smp::invoke_on_all(
        [&max_val_size_t]() -> seastar::future<> {
            CacheEntryPool pool = CacheEntryPool(max_val_size_t);
            auto policy = make_test_policy();
            co_await pool.init(test_config, policy);
            uint64_t pool_size_by_mem = pool.calculate_optimal_pool_size();
            BOOST_REQUIRE_EQUAL(pool.get_total_slots(), pool_size_by_mem);
            BOOST_REQUIRE_EQUAL(pool.get_total_slots(),
                                pool.get_available_slots());
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
        CacheEntryPool pool = CacheEntryPool(pool_size);
        auto policy = make_test_policy();
        co_await pool.init(test_config, policy);

        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == pool_size);

        vector acquired_entry;

        /* Acquire slots */
        acquired_entry = co_await acquireSlots(pool_size, pool);

        BOOST_REQUIRE_EQUAL(acquired_entry.size(), pool_size);
        BOOST_REQUIRE_EQUAL(pool.get_available_slots(), 0);

        /* Release slots */
        releaseSlots(acquired_entry, pool);

        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == pool_size);

        co_return;
    });
    co_return;
}

SEASTAR_TEST_CASE(object_pool_multiple_init) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        uint32_t pool_size = 32;
        CacheEntryPool pool = CacheEntryPool(pool_size);
        auto policy = make_test_policy();
        co_await pool.init(test_config, policy);
        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == pool_size);
        /* init again */
        co_await pool.init(test_config, policy);
        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == pool_size);
    });
}

SEASTAR_TEST_CASE(object_pool_acquire_overflow) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        uint32_t pool_size = 32;
        CacheEntryPool pool = CacheEntryPool(pool_size);
        auto policy = make_test_policy();
        co_await pool.init(test_config, policy);
        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == pool_size);
        vector acquired_slots = co_await acquireSlots(pool_size, pool);
        BOOST_REQUIRE_EQUAL(acquired_slots.size(), pool_size);
        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == 0);
        /* Now the pool is exhausted. Acquire more slots */
        std::unique_ptr<ttl::Entry> extra_entry = co_await pool.acquire();
        BOOST_REQUIRE(extra_entry.get() != nullptr);
        BOOST_REQUIRE(extra_entry->is_in_use());
        /* This value was fetched from outside the pool, and since the pool is
         * empty now, releasing it will add it to the pool
         */
        pool.release(std::move(extra_entry));
        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == 1);
    });
}

SEASTAR_TEST_CASE(object_pool_release_overflow) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        uint32_t pool_size = 32;
        CacheEntryPool pool = CacheEntryPool(pool_size);
        auto policy = make_test_policy();
        co_await pool.init(test_config, policy);
        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == pool_size);
        /* make a unique pointer of Entry and release it to the pool */
        std::unique_ptr<ttl::Entry> extra_entry =
            std::make_unique<ttl::Entry>();
        pool.release(std::move(extra_entry));
        extra_entry = std::make_unique<ttl::Entry>();
        pool.release(std::move(extra_entry));
        extra_entry = std::make_unique<ttl::Entry>();
        pool.release(std::move(extra_entry));
        extra_entry = std::make_unique<ttl::Entry>();
        pool.release(std::move(extra_entry));
        extra_entry = std::make_unique<ttl::Entry>();
        pool.release(std::move(extra_entry));
        extra_entry = std::make_unique<ttl::Entry>();
        pool.release(std::move(extra_entry));
        extra_entry = std::make_unique<ttl::Entry>();
        pool.release(std::move(extra_entry));
        extra_entry = std::make_unique<ttl::Entry>();
        pool.release(std::move(extra_entry));
        /* since the pool is at max size, it should not add this entry to the
         * pool */
        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == pool_size);
    });
}

SEASTAR_TEST_CASE(object_pool_mutate_fields) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        uint32_t pool_size = 1;
        CacheEntryPool pool = CacheEntryPool(pool_size);
        auto policy = make_test_policy();
        co_await pool.init(test_config, policy);
        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == pool_size);

        std::unique_ptr<ttl::Entry> entry = co_await pool.acquire();
        BOOST_REQUIRE(pool.get_available_slots() == 0);
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
        pool.release(std::move(entry));
        /* Ensure it is released */
        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == pool_size);
        /* acquire the entry again. Since the pool size is 1, it is supposed to
         * return the same memory. We need to ensure that the data is removed.
         */
        entry = co_await pool.acquire();
        BOOST_REQUIRE(pool.get_available_slots() == 0);
        BOOST_REQUIRE(entry->key != "test_key");
        BOOST_REQUIRE(entry->value != "test_value");
        BOOST_REQUIRE(entry->expires_at != epoch_now);
        BOOST_REQUIRE(entry->last_access != epoch_now);
        BOOST_REQUIRE(entry->ver != 5);
        BOOST_REQUIRE(entry->heat != 20);
        BOOST_REQUIRE(entry->visited == false);

        pool.release(std::move(entry));
        /* Released, so available should be equal to pool_size */
        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == pool_size);
    });
}

SEASTAR_TEST_CASE(object_pool_distinct_pointer) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        uint32_t pool_size = 2048;
        CacheEntryPool pool = CacheEntryPool(pool_size);
        auto policy = make_test_policy();
        co_await pool.init(test_config, policy);
        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == pool_size);

        vector acquired_slots = co_await acquireSlots(pool_size, pool);
        BOOST_REQUIRE_EQUAL(acquired_slots.size(), pool_size);
        BOOST_REQUIRE(pool.get_total_slots() == pool_size &&
                      pool.get_available_slots() == 0);
    });
}

SEASTAR_TEST_CASE(object_pool_promote_to_sanctuary_tracks_sieve) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        CacheEntryPool pool = CacheEntryPool(4);
        auto policy = make_test_policy();
        co_await pool.init(test_config, policy);

        auto entry = co_await pool.acquire();
        BOOST_REQUIRE(entry);
        BOOST_REQUIRE_EQUAL(policy.size(), 0);

        pool.promote_to_sanctuary(*entry);

        BOOST_REQUIRE_EQUAL(policy.size(), 1);
        BOOST_REQUIRE(entry->pool_type == ttl::PoolType::Sanctuary);

        pool.release(std::move(entry));
        BOOST_REQUIRE_EQUAL(policy.size(), 0);
        co_return;
    });
    co_return;
}

SEASTAR_TEST_CASE(object_pool_reused_entry_relinks_into_sanctuary_when_probation_disabled) {
    co_await seastar::smp::invoke_on_all([]() -> seastar::future<> {
        CacheEntryPool pool = CacheEntryPool(1);
        auto policy = make_test_policy();
        const db_config cfg = make_zero_probation_config();
        co_await pool.init(cfg, policy);

        auto first = co_await pool.acquire();
        BOOST_REQUIRE(first);
        BOOST_REQUIRE_EQUAL(policy.size(), 1);
        BOOST_REQUIRE(first->pool_type == ttl::PoolType::Sanctuary);

        pool.release(std::move(first));
        BOOST_REQUIRE_EQUAL(policy.size(), 0);

        auto second = co_await pool.acquire();
        BOOST_REQUIRE(second);
        BOOST_REQUIRE_EQUAL(policy.size(), 1);
        BOOST_REQUIRE(second->pool_type == ttl::PoolType::Sanctuary);

        pool.release(std::move(second));
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
