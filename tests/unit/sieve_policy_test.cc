/**
 * Unit test case for sieve eviction policy.
 */
#include "eviction/eviction_config.hh"
#include "eviction/sieve_policy.hh"
#include "ttl/entry.hh"
#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <seastar/testing/test_case.hh>

/**
 * Declare a map to store the entries and trigger hit
 */
using map = absl::flat_hash_map<seastar::sstring, ttl::Entry>;
using set = absl::flat_hash_set<seastar::sstring>;
using vector = std::vector<seastar::sstring>;
map map_;

inline uint64_t now_s() {
    using namespace std::chrono;
    return duration_cast<seconds>(steady_clock::now().time_since_epoch())
        .count();
}

SievePolicy init(uint32_t budget) {
    map_.clear();
    map_.reserve(100);
    // Create an instance of sieve_policy_
    eviction::EvictionConfig ev_cfg{
        .policy = eviction::EvictionPolicy::Sieve,
        .soft_ =
            {
                .trigger = 0.6,
                .stop = 0.5,
                .budget = budget,
                .throttle = false,
            },
        // .hard_ =
        //     {
        //         .trigger = 0.8,
        //         .stop = 0.7,
        //         .budget = budget,
        //         .throttle = true,
        //     },
    };

    auto sieve_policy = SievePolicy(ev_cfg);
    return sieve_policy;
}

void createEntry(uint32_t start, uint32_t end, SievePolicy &policy) {
    for (uint32_t i = start; i <= end; i++) {
        std::string str_offset = std::to_string(i);
        seastar::sstring key = "test_key_" + str_offset;
        auto [it, inserted] = map_.emplace(key, ttl::Entry{});
        it->second.value = "test_value_" + str_offset;
        it->second.key = key;
        it->second.expires_at = 0;
        it->second.ver = 0;
        it->second.heat = 0;
        it->second.last_access = 0;
        it->second.visited = false;
        policy.on_insert(it->second);
    }
}

void createEntry(uint32_t entry_count, SievePolicy &policy) {
    createEntry(1, entry_count, policy);
}

// start and end are inclusive
set hitEntry(int start, int end, SievePolicy &policy) {
    set hit_keys;
    for (int i = start; i <= end; ++i) {
        seastar::sstring key = "test_key_" + std::to_string(i);
        auto it = map_.find(key);
        if (it == map_.end())
            continue;
        hit_keys.insert(key);
        policy.on_hit(it->second);
    }
    return hit_keys;
}

// start and end are inclusive
set eraseEntry(int start, int end, SievePolicy &policy) {
    set erased_keys;
    for (int i = start; i <= end; i++) {
        std::string str_offset = std::to_string(i);
        seastar::sstring key = "test_key_" + str_offset;
        auto it = map_.find(key);
        if (it == map_.end())
            continue;
        policy.on_erase(it->second); // unlink while still in map storage
        erased_keys.insert(key);
        map_.erase(it);
    }
    return erased_keys;
}

/**
 * Test: sieve_policy_eviction_lower_budget
 *
 * Scenario: Higher unvisited ,lower eviction budget. Ensures that we never
 * evict more than the budget per iteration.
 *
 * Setup:
 *   Insert 20 entries
 *   eviction_budget = 3
 *
 * Action:
 *   Mark 15 entries as visited
 *   Trigger eviction
 *
 * Expected:
 *   3 victims returned
 *   Victims correspond to unvisited entries
 */

SEASTAR_TEST_CASE(sieve_policy_eviction_lower_budget) {
    SievePolicy policy = init(3);
    vector victim_list;
    // Create entry
    createEntry(20, policy);
    // trigger evict once because algo protects keys for first run
    victim_list = co_await policy.evict(now_s());
    BOOST_REQUIRE_EQUAL(victim_list.size(), 0);
    // Trigger hit for first 15 keys
    set hit_keys = hitEntry(1, 15, policy);
    BOOST_REQUIRE_EQUAL(hit_keys.size(), 15);
    // Trigger eviction
    victim_list = co_await policy.evict(now_s());
    BOOST_REQUIRE_EQUAL(victim_list.size(), 3);
    for (auto &victim : victim_list) {
        BOOST_REQUIRE(!hit_keys.contains(victim));
    }
}

/**
 * Test: sieve_policy_eviction_higher_budget
 *
 * Scenario:
 *   Lower unvisited, higher eviction budget. Ensures that we don't evict any
 * visited node just because we have budget surplus.
 *
 * Setup:
 *   Entry = 20, budget = 10
 *
 * Action:
 *   Hit the first 15 keys, then perform eviction
 *
 * Expected:
 *   5 entries evicted
 */
SEASTAR_TEST_CASE(sieve_policy_eviction_higher_budget) {
    SievePolicy policy = init(10);

    vector victim_list;
    // Create entry
    createEntry(20, policy);
    // trigger evict once because algo protects keys for first run
    victim_list = co_await policy.evict(now_s());
    BOOST_REQUIRE_EQUAL(victim_list.size(), 0);
    // Trigger hit for first 15 keys
    set hit_keys = hitEntry(1, 15, policy);
    BOOST_REQUIRE_EQUAL(hit_keys.size(), 15);
    // Trigger eviction
    victim_list = co_await policy.evict(now_s());
    BOOST_REQUIRE_EQUAL(victim_list.size(), 5);
    for (auto &victim : victim_list) {
        BOOST_REQUIRE(!hit_keys.contains(victim));
    }
}

/**
 * Test: sieve_policy_no_entry
 *
 * Scenario:
 *   There are no entries in the list. Ensures that the logic doesn't crash when
 * nothing is present.
 *
 * Setup:
 *   Entry = 0, budget = 10;
 *
 * Action:
 *   perform eviction
 *
 * Expected:
 *   zero eviction count
 */

SEASTAR_TEST_CASE(sieve_policy_no_entry) {
    SievePolicy policy = init(10);

    vector victim_list;
    victim_list = co_await policy.evict(now_s());
    BOOST_REQUIRE_EQUAL(victim_list.size(), 0);
}

/**
 * Test: sieve_policy_eviction_with_keys_deletion_scenario_I
 *
 * Scenario:
 *   Delete keys from the list and then perform eviction. Ensures that the
 * deletion updates the links and statuses correctly.
 *
 * Setup:
 *   Entry = 20, budget = 20
 *
 * Action:
 *   hit the first 10 keys, delete the first 5 keys
 *
 * Expected:
 *   evcition count should be 10 and should not be in keys deleted nor in the
 * keys that were hit.
 */

SEASTAR_TEST_CASE(sieve_policy_eviction_with_keys_deletion_scenario_I) {
    SievePolicy policy = init(20);
    createEntry(20, policy);
    vector victim_list = co_await policy.evict(now_s());
    BOOST_REQUIRE_EQUAL(victim_list.size(), 0);
    set hit_keys = hitEntry(1, 10, policy);
    BOOST_REQUIRE_EQUAL(hit_keys.size(), 10);
    set erased_keys = eraseEntry(1, 5, policy);
    BOOST_REQUIRE_EQUAL(erased_keys.size(), 5);
    victim_list = co_await policy.evict(now_s());
    BOOST_REQUIRE_EQUAL(victim_list.size(), 10);
    for (const auto &victim : victim_list) {
        BOOST_REQUIRE(!erased_keys.contains(victim));
        BOOST_REQUIRE(!hit_keys.contains(victim));
    }
}

/**
 * Test: sieve_policy_no_eviction
 *
 * Scenario:
 *   All the nodes are visited, so no eviction is done. Ensures that we don't
 * evict anything when all nodes are active.
 *
 * Setup:
 *   Entry = 20, budget = 20
 *
 * Action:
 *   Hit all the entries, trigger eviction
 *
 * Expected:
 *   zero eviction count
 */
SEASTAR_TEST_CASE(sieve_policy_no_eviction) {
    SievePolicy policy = init(20);
    createEntry(20, policy);
    vector victim_list = co_await policy.evict(now_s());
    BOOST_REQUIRE_EQUAL(victim_list.size(), 0);
    set hit_keys = hitEntry(1, 20, policy);
    BOOST_REQUIRE_EQUAL(hit_keys.size(), 20);
    set erased_keys = eraseEntry(1, 5, policy);
    BOOST_REQUIRE_EQUAL(erased_keys.size(), 5);
    victim_list = co_await policy.evict(now_s());
    BOOST_REQUIRE_EQUAL(victim_list.size(), 0);
}

/**
 * Test: sieve_policy_with_keys_deletion_scenario_II
 *
 * Scenario:
 *   disjoint hit and delete
 *
 * Setup:
 *   Entry 20, budget 20
 *
 * Action:
 *   Hit the last 10 keys, delete the first 5 keys, perform eviction
 *
 * Expected:
 *   eviction count should be 5
 */

SEASTAR_TEST_CASE(sieve_policy_with_keys_deletion_scenario_II) {
    SievePolicy policy = init(20);
    createEntry(20, policy);
    vector victim_list = co_await policy.evict(now_s());
    BOOST_REQUIRE_EQUAL(victim_list.size(), 0);
    set hit_keys = hitEntry(11, 20, policy);
    BOOST_REQUIRE_EQUAL(hit_keys.size(), 10);
    set erased_keys = eraseEntry(1, 5, policy);
    BOOST_REQUIRE_EQUAL(erased_keys.size(), 5);
    victim_list = co_await policy.evict(now_s());
    BOOST_REQUIRE_EQUAL(victim_list.size(), 5);
    for (const auto &victim : victim_list) {
        BOOST_REQUIRE(!erased_keys.contains(victim));
        BOOST_REQUIRE(!hit_keys.contains(victim));
    }
}

/**
 * Test: sieve_policy_hand_wrap_around
 *
 * Scenario:
 * The eviction hand reaches the end of the list and must wrap back to the head
 * to find more victims in a subsequent call.
 *
 * Setup:
 * Insert 10 entries, budget = 5.
 *
 * Action:
 * 1. First eviction pass clears the visited bit on the initial 10 entries.
 * 2. Hit keys 6-10 to protect them.
 * 3. Insert 5 NEW entries (11-15) at the tail.
 * 4. Evict again.
 *
 * Expected:
 * The hand should advance past the protected keys, wrap if needed, and evict
 * five unprotected keys.
 */
SEASTAR_TEST_CASE(sieve_policy_hand_wrap_around) {
    SievePolicy policy = init(5);
    vector victim_list;

    // 1. Setup initial state (Keys 1-10)
    createEntry(10, policy);

    // 2. Initial eviction clears the visited bits on the first pass.
    victim_list = co_await policy.evict(now_s());
    BOOST_REQUIRE_EQUAL(victim_list.size(), 0);

    // 3. Protect the last 5 original keys.
    set protected_keys = hitEntry(6, 10, policy);
    BOOST_REQUIRE_EQUAL(protected_keys.size(), 5);

    // 4. Insert 5 NEW keys (Keys 11-15)
    createEntry(11, 15, policy);

    // 5. Evict with a budget of 5
    // The hand must traverse the protected keys and find the unprotected old
    // entries first.
    victim_list = co_await policy.evict(now_s());

    // The expected victims are the old unprotected keys 1-5.
    BOOST_REQUIRE_EQUAL(victim_list.size(), 5);

    for (auto &victim : victim_list) {
        // Ensure we didn't evict the ones we just 'hit'
        BOOST_REQUIRE(!protected_keys.contains(victim));
    }
}
