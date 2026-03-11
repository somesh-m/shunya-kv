cmake_minimum_required (VERSION 3.5)

project (SeastarExample)

option(BUILD_SERVER "Build the shunya_store server" ON)
option(ENABLE_TESTS "Build and run tests" ON)
option(ENABLE_HOT_PATH_DEBUG "Enable request forwarding counters and latency measurement in hot path" OFF)
option(ENABLE_SANITIZERS "Enable ASan/UBSan instrumentation for shunya_store" OFF)
option(ENABLE_LINT_TARGETS "Enable clang-format/clang-tidy targets" ON)

# ---- C++ standard / build type ----
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Choose the type of build." FORCE)
endif()

# ---- Paths ----
set(SHUNYA_SRC_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SHUNYA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SHUNYA_TEST_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/tests")

if (ENABLE_LINT_TARGETS)
file(GLOB_RECURSE SHUNYA_TIDY_SOURCES CONFIGURE_DEPENDS
  "${SHUNYA_SRC_DIR}/*.cc"
  "${SHUNYA_TEST_DIR}/*.cc"
)
list(FILTER SHUNYA_TIDY_SOURCES EXCLUDE REGEX ".*/metrics_logger\\.cc$")
list(FILTER SHUNYA_TIDY_SOURCES EXCLUDE REGEX ".*/service\\.cc$")
list(FILTER SHUNYA_TIDY_SOURCES EXCLUDE REGEX ".*/server\\.cc$")

file(GLOB_RECURSE SHUNYA_FORMAT_SOURCES CONFIGURE_DEPENDS
  "${SHUNYA_SRC_DIR}/*.cc"
  "${SHUNYA_SRC_DIR}/*.hh"
  "${SHUNYA_INCLUDE_DIR}/*.hh"
  "${SHUNYA_TEST_DIR}/*.cc"
  "${SHUNYA_TEST_DIR}/*.hh"
)

find_program(CLANG_FORMAT_EXE NAMES clang-format)
find_program(CLANG_TIDY_EXE NAMES clang-tidy)

if(CLANG_FORMAT_EXE)
  add_custom_target(format
    COMMAND "${CLANG_FORMAT_EXE}" -i ${SHUNYA_FORMAT_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Formatting C++ sources with clang-format"
    VERBATIM
  )

  add_custom_target(format-check
    COMMAND "${CLANG_FORMAT_EXE}" --dry-run --Werror ${SHUNYA_FORMAT_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Checking C++ formatting with clang-format"
    VERBATIM
  )
else()
  add_custom_target(format
    COMMAND "${CMAKE_COMMAND}" -E echo "clang-format not found"
    COMMAND "${CMAKE_COMMAND}" -E false
    VERBATIM
  )
  add_custom_target(format-check
    COMMAND "${CMAKE_COMMAND}" -E echo "clang-format not found"
    COMMAND "${CMAKE_COMMAND}" -E false
    VERBATIM
  )
endif()

if(CLANG_TIDY_EXE)
  add_custom_target(tidy
    COMMAND "${CLANG_TIDY_EXE}"
            -p "${CMAKE_BINARY_DIR}"
            --header-filter="^${CMAKE_SOURCE_DIR}/(src|include|tests)/.*"
            --system-headers=false
            ${SHUNYA_TIDY_SOURCES}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Running clang-tidy"
    VERBATIM
  )
else()
  add_custom_target(tidy
    COMMAND "${CMAKE_COMMAND}" -E echo "clang-tidy not found"
    COMMAND "${CMAKE_COMMAND}" -E false
    VERBATIM
  )
endif()
endif()

# =========================================
# FNV reference (vendored from lcn2/fnv)
# =========================================
# Some repos name it fnv.c; if you copied fnv64.c instead, we’ll fall back.
set(FNV_REF_C "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fnv/hash_64a.c")
if (NOT EXISTS "${FNV_REF_C}")
  set(FNV_REF_C "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fnv/hash_32a.c")
endif()

add_library(fnv_ref STATIC "${FNV_REF_C}")
target_include_directories(fnv_ref PUBLIC third_party/fnv)
set_target_properties(fnv_ref PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- Deps ----
find_package (Seastar CONFIG REQUIRED)
message(STATUS "Seastar found: ${Seastar_DIR}")
message(STATUS "Seastar version: ${Seastar_VERSION}")

#find_library(JEMALLOC_LIB jemalloc)
#if(JEMALLOC_LIB)
#  message(STATUS "jemalloc found: ${JEMALLOC_LIB}")
#else()
#  message(WARNING "jemalloc not found; shunya_store will link with the default allocator")
#endif()


find_package(fmt 9 REQUIRED)
find_package(absl CONFIG QUIET)
if (NOT absl_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    abseil_cpp
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG 20240116.2
  )
  FetchContent_MakeAvailable(abseil_cpp)
endif()


# ---- proto_helpers lib ----
add_library(proto_helpers STATIC
  ${SHUNYA_SRC_DIR}/proto_helpers.cc
  ${SHUNYA_INCLUDE_DIR}/proto_helpers.hh
)
target_link_libraries(proto_helpers PUBLIC Seastar::seastar)
target_include_directories(proto_helpers PUBLIC ${SHUNYA_INCLUDE_DIR})

# =========================================
# Shunya store
# =========================================
file(GLOB_RECURSE SHUNYA_SOURCES CONFIGURE_DEPENDS
  "${SHUNYA_SRC_DIR}/*.cc"
)

# Keep currently disabled sources excluded from the server target.
list(FILTER SHUNYA_SOURCES EXCLUDE REGEX ".*/metrics_logger\\.cc$")
list(FILTER SHUNYA_SOURCES EXCLUDE REGEX ".*/service\\.cc$")
list(FILTER SHUNYA_SOURCES EXCLUDE REGEX ".*/server\\.cc$")



if (BUILD_SERVER)
add_executable(shunya_store ${SHUNYA_SOURCES})

target_include_directories(shunya_store PRIVATE
  ${SHUNYA_INCLUDE_DIR}
  ${SHUNYA_SRC_DIR}
  third_party/fnv
)

target_compile_options(shunya_store PRIVATE
  $<$<CONFIG:Debug>:-O0 -g3 -fno-omit-frame-pointer -fno-inline>
  $<$<CONFIG:Release>:-O3 -g0>
  $<$<CONFIG:RelWithDebInfo>:-O3 -g1 -fno-omit-frame-pointer>
  $<$<CONFIG:MinSizeRel>:-Os -g0>
  $<$<CONFIG:PerfProfile>:-O3 -g1 -fno-omit-frame-pointer>
)

target_compile_definitions(shunya_store PRIVATE
  $<$<BOOL:${ENABLE_HOT_PATH_DEBUG}>:SHUNYAKV_ENABLE_HOT_PATH_DEBUG=1>
  $<$<NOT:$<BOOL:${ENABLE_HOT_PATH_DEBUG}>>:SHUNYAKV_ENABLE_HOT_PATH_DEBUG=0>
)

target_link_libraries(shunya_store PRIVATE
  Seastar::seastar
  fmt::fmt
  proto_helpers
  fnv_ref            # <-- link the official FNV ref
  absl::flat_hash_map
)
if(JEMALLOC_LIB)
  target_link_libraries(shunya_store PRIVATE ${JEMALLOC_LIB})
endif()

if (ENABLE_SANITIZERS)
  target_compile_options(shunya_store PRIVATE -fsanitize=address,undefined)
  target_link_options(shunya_store PRIVATE -fsanitize=address,undefined)
endif()

endif()

if (ENABLE_TESTS)
enable_testing()
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

SET(TEST_SOURCES
  ${SHUNYA_TEST_DIR}/hashtest.cc
  ${SHUNYA_TEST_DIR}/test_hash_wrapper.cc
  # ${SHUNYA_TEST_DIR}/ttl_cache_test.cc
)
add_executable(shunya_test
  ${TEST_SOURCES}
  # ${SHUNYA_SRC_DIR}/ttl/ttl_cache.cc
  ${SHUNYA_SRC_DIR}/pool/object_pool.cc
  ${SHUNYA_SRC_DIR}/eviction/sieve_policy.cc
)
# Make sure tests can include your headers + vendored fnv
target_include_directories(shunya_test PRIVATE
  ${SHUNYA_INCLUDE_DIR}        # e.g. include/hash_wrapper.hh
  third_party/fnv              # fnv.h + longlong.h
)

# Link gtest and your fnv_ref (and anything else your test needs)
target_link_libraries(shunya_test PRIVATE
  GTest::gtest GTest::gtest_main
  Seastar::seastar
  fnv_ref
  # fmt::fmt         # if your test uses fmt
  # Seastar::seastar # only if your test touches seastar
)
include(GoogleTest)
gtest_discover_tests(shunya_test)
add_subdirectory(tests/unit)
endif()
