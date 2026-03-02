#include "ttl/ttl_cache.hh"
#include <gtest/gtest.h>

struct FakeClock {
    uint64_t t = 0;
    uint64_t now() const { return t; }
    void advance(uint64_t d) { t += d; }
};

TEST(TtlCache, SetGetBeforeExpiry) {
    FakeClock c;
    ttl::TtlCache cache(nullptr);

    cache.set("key", "value", c.now(), 1000).get();
    auto v = cache.get("key", c.now()).get();

    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, "value");
}

TEST(TtlCache, ExpiredKeyMissAndErasedOnGet) {
    FakeClock c;
    ttl::TtlCache cache(nullptr);

    cache.set("key", "value", c.now(), 1000).get();

    c.advance(1000);
    auto v = cache.get("key", c.now()).get();

    EXPECT_FALSE(v.has_value());
    EXPECT_EQ(cache.size(), 0u);
}

TEST(TtlCache, EvictRemovesExpired) {
    FakeClock c;
    ttl::TtlCache cache(nullptr);

    cache.set("a", "1", c.now(), 10).get();
    cache.set("b", "2", c.now(), 50).get();

    c.advance(11); // "a" expired, "b" not yet
    auto removed = cache.evict(c.now(), 1000);

    EXPECT_EQ(removed, 1u);
    EXPECT_FALSE(cache.get("a", c.now()).get().has_value());
    EXPECT_TRUE(cache.get("b", c.now()).get().has_value());
}

TEST(TtlCache, LazyHeapInvalidationOnUpdate) {
    // This test proves we DON'T need to search/remove old heap nodes.
    FakeClock c;
    ttl::TtlCache cache(nullptr);

    cache.set("k", "v1", c.now(), 100).get(); // expiry at t=100
    // overwrite same key with longer TTL -> old heap node becomes stale
    cache.set("k", "v2", c.now(), 1000).get(); // expiry at t=1000

    c.advance(150); // old expiry passed

    // If lazy invalidation works, eviction should NOT delete "k"
    cache.evict(c.now(), 1000);

    auto v = cache.get("k", c.now()).get();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, "v2");
}

TEST(TtlCache, SlidingPolicyExtendsExpiry) {
    FakeClock c;
    ttl::ttl_policy policy;
    policy.mode = ttl::Mode::Sliding;
    policy.base_ttl = 100; // each hit extends to now+100

    ttl::TtlCache cache(&policy);
    cache.set("k", "v", c.now(), 50).get(); // initially expires at 50

    c.advance(40);
    ASSERT_TRUE(cache.get("k", c.now()).get().has_value()); // extends expiry to 140

    c.advance(60); // now at 100, would have expired without extension
    ASSERT_TRUE(cache.get("k", c.now()).get().has_value());
}

TEST(TtlCache, ExtendOnlyNearExpiryThreshold) {
    FakeClock c;
    ttl::ttl_policy policy;
    policy.mode = ttl::Mode::Sliding;
    policy.base_ttl = 1000;

    ttl::TtlCache cache(&policy);
    cache.set_extend_threshold(200); // only extend if remaining < 200

    cache.set("k", "v", c.now(), 1000).get(); // expires at 1000

    c.advance(100); // remaining=900, should NOT extend
    ASSERT_TRUE(cache.get("k", c.now()).get().has_value());

    // move close to expiry
    c.advance(850); // now=950, remaining=50 -> should extend to 1950
    ASSERT_TRUE(cache.get("k", c.now()).get().has_value());

    c.advance(900); // now=1850, still alive if extension happened
    ASSERT_TRUE(cache.get("k", c.now()).get().has_value());
}
