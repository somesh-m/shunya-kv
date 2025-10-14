#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "fnv.h"   // vendored upstream header

static uint64_t H_buf(const void* p, size_t n) {
#if defined(FNV1A_64_INIT)
    return (uint64_t)fnv_64a_buf((void*)p, n, FNV1A_64_INIT);
#else
    return (uint64_t)fnv_64a_buf((void*)p, n, FNV1_64A_INIT);
#endif
}

int main(void) {
    /* Canonical vectors */
    assert(H_buf("", 0)       == 0xcbf29ce484222325ULL);
    assert(H_buf("a", 1)      == 0xaf63dc4c8601ec8cULL);
    assert(H_buf("hello", 5)  == 0xa430d84680aabd0bULL);
    assert(H_buf("kname", 5)  == 17675175617450891793ULL);

    /* Embedded NULs */
    { const char s[] = {0}; assert(H_buf(s, 1) == 0xaf63bd4c8601b7dfULL); }
    { const char s[] = {'a','b','c','\0','d','e','f'};
      assert(H_buf(s, sizeof s) == 0xe34a003877932188ULL); }

    return 0;
}
