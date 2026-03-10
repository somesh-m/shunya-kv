#pragma once

#include <chrono>

// Enable this via compiler flags: -DSHUNYAKV_ENABLE_HOT_PATH_DEBUG=1
#ifndef SHUNYAKV_ENABLE_HOT_PATH_DEBUG
#define SHUNYAKV_ENABLE_HOT_PATH_DEBUG 0
#endif

#if SHUNYAKV_ENABLE_HOT_PATH_DEBUG
#define HOTPATHLOGS(stmt)                                                      \
    do {                                                                       \
        stmt;                                                                  \
    } while (0)
#else
#define HOTPATHLOGS(stmt)                                                      \
    do {                                                                       \
    } while (0)
#endif

#if SHUNYAKV_ENABLE_HOT_PATH_DEBUG
#define HOTPATH_START(start)                                                   \
    const auto start = std::chrono::steady_clock::now();

#define HOTPATH_END(start, call)                                               \
    call(std::chrono::duration_cast<std::chrono::microseconds>(                \
             std::chrono::steady_clock::now() - start)                         \
             .count());
#else
#define HOTPATH_START(start)
#define HOTPATH_END(start, call)
#endif

#if SHUNYAKV_ENABLE_HOT_PATH_DEBUG

#define HOTPATHCOUNT(stmt)                                                     \
    do {                                                                       \
        stmt;                                                                  \
    } while (0)

#else

#define HOTPATHCOUNT(stmt)                                                     \
    do {                                                                       \
    } while (0)

#endif
