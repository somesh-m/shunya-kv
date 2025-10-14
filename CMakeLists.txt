cmake_minimum_required (VERSION 3.5)

project (SeastarExample)

option(BUILD_SERVER "Build the shunya_store server" ON)
option(ENABLE_TESTS "Build and run tests" ON)

# ---- C++ standard / build type ----
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

if (BUILD_SERVER)
# ---- Deps ----
find_package (Seastar CONFIG REQUIRED)
message(STATUS "Seastar found: ${Seastar_DIR}")
message(STATUS "Seastar version: ${Seastar_VERSION}")

find_package(fmt 9 REQUIRED)

# ---- Paths ----
set(SHUNYA_SRC_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SHUNYA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(SHUNYA_TEST_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/tests")

# ---- proto_helpers lib ----
add_library(proto_helpers STATIC
  ${SHUNYA_SRC_DIR}/proto_helpers.cc
  ${SHUNYA_INCLUDE_DIR}/proto_helpers.hh
)
target_link_libraries(proto_helpers PUBLIC Seastar::seastar)
target_include_directories(proto_helpers PUBLIC ${SHUNYA_INCLUDE_DIR})

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

# =========================================
# Shunya store
# =========================================
set(SHUNYA_SOURCES
  ${SHUNYA_SRC_DIR}/main.cc
  ${SHUNYA_SRC_DIR}/server_debug.cc
  ${SHUNYA_SRC_DIR}/server.cc
  ${SHUNYA_SRC_DIR}/commands.cc
  ${SHUNYA_SRC_DIR}/cmd_set.cc
  ${SHUNYA_SRC_DIR}/cmd_get.cc
  ${SHUNYA_SRC_DIR}/cmd_quit.cc
  ${SHUNYA_SRC_DIR}/cmd_node_info.cc
  ${SHUNYA_SRC_DIR}/router.cc
  ${SHUNYA_SRC_DIR}/kv_store.cc
  ${SHUNYA_SRC_DIR}/hash.cc
  ${SHUNYA_SRC_DIR}/hash_wrapper.cc   # keep if you really have/need this C wrapper
  # ${SHUNYA_SRC_DIR}/metrics_logger.cc
  # ${SHUNYA_SRC_DIR}/service.cc
)

add_executable(shunya_store ${SHUNYA_SOURCES})

target_include_directories(shunya_store PRIVATE
  ${SHUNYA_INCLUDE_DIR}
  ${SHUNYA_SRC_DIR}
  third_party/fnv
)

target_compile_options(shunya_store PRIVATE
  -O0 -g3 -fno-omit-frame-pointer -fno-inline
)
target_compile_definitions(shunya_store PRIVATE -DSEASTAR_API_LEVEL=8)

target_link_libraries(shunya_store PRIVATE
  Seastar::seastar
  fmt::fmt
  proto_helpers
  fnv_ref            # <-- link the official FNV ref
)

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
)
add_executable(shunya_test ${TEST_SOURCES})
# Make sure tests can include your headers + vendored fnv
target_include_directories(shunya_test PRIVATE
  ${SHUNYA_INCLUDE_DIR}        # e.g. include/hash_wrapper.hh
  third_party/fnv              # fnv.h + longlong.h
)

# Link gtest and your fnv_ref (and anything else your test needs)
target_link_libraries(shunya_test PRIVATE
  GTest::gtest GTest::gtest_main
  fnv_ref
  # fmt::fmt         # if your test uses fmt
  # Seastar::seastar # only if your test touches seastar
)
include(GoogleTest)
gtest_discover_tests(shunya_test)
endif()
