#include <gtest/gtest.h>
extern "C" {
#include "fnv.h"
}

static uint64_t H_buf(const void *p, size_t n) {
#if defined(FNV1A_64_INIT)
    return (uint64_t)fnv_64a_buf((void *)p, n, FNV1A_64_INIT);
#else
    return (uint64_t)fnv_64a_buf((void *)p, n, FNV1_64A_INIT);
#endif
}

TEST(FNVRef, CanonicalVectors) {
    EXPECT_EQ(H_buf("", 0), 0xcbf29ce484222325ULL);
    EXPECT_EQ(H_buf("a", 1), 0xaf63dc4c8601ec8cULL);
    EXPECT_EQ(H_buf("hello", 5), 0xa430d84680aabd0bULL);
    EXPECT_EQ(H_buf("kname", 5), 17675175617450891793ULL);
}

TEST(FNVRef, EmbeddedNulls) {
    const char s1[] = {0};
    EXPECT_EQ(H_buf(s1, 1), 0xaf63bd4c8601b7dfULL);

    const char s2[] = {'a', 'b', 'c', '\0', 'd', 'e', 'f'};
    EXPECT_EQ(H_buf(s2, sizeof s2), 0xe34a003877932188ULL);
}
