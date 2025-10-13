cmake_minimum_required (VERSION 3.5)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_BUILD_TYPE Debug)

project (SeastarExample)

find_package (Seastar CONFIG REQUIRED)
message(STATUS "Seastar found: ${Seastar_DIR}")
message(STATUS "Seastar version: ${Seastar_VERSION}")

find_package(fmt 9 REQUIRED)

set(SHUNYA_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(SHUNYA_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")

add_library(proto_helpers STATIC
  ${SHUNYA_SRC_DIR}/proto_helpers.cc
  ${SHUNYA_INCLUDE_DIR}/proto_helpers.hh
)
target_link_libraries(proto_helpers PUBLIC Seastar::seastar)
target_include_directories(proto_helpers PUBLIC ${SHUNYA_INCLUDE_DIR})

set(SHUNYA_SOURCES
  ${SHUNYA_SRC_DIR}/main.cc
  ${SHUNYA_SRC_DIR}/server_debug.cc
  ${SHUNYA_SRC_DIR}/server.cc
  ${SHUNYA_SRC_DIR}/commands.cc
  ${SHUNYA_SRC_DIR}/cmd_set.cc
  ${SHUNYA_SRC_DIR}/cmd_get.cc
  ${SHUNYA_SRC_DIR}/cmd_quit.cc
  ${SHUNYA_SRC_DIR}/router.cc
  ${SHUNYA_SRC_DIR}/kv_store.cc
  ${SHUNYA_SRC_DIR}/hash.cc

  # ${SHUNYA_SRC_DIR}/metrics_logger.cc
  # ${SHUNYA_SRC_DIR}/service.cc
)

add_executable(shunya_store ${SHUNYA_SOURCES})

# ✅ Add your header search paths so "server.hh" is found
target_include_directories(shunya_store PRIVATE
  ${SHUNYA_INCLUDE_DIR}
  ${SHUNYA_SRC_DIR}
)
# ✅ Add compile options to make debugging easier
target_compile_options(shunya_store PRIVATE
  -O0 -g3 -fno-omit-frame-pointer -fno-inline
)
target_compile_definitions(shunya_store PRIVATE -DSEASTAR_API_LEVEL=8)

# ✅ Link helpers + seastar + fmt
target_link_libraries(shunya_store PRIVATE
  Seastar::seastar
  fmt::fmt
  proto_helpers
)
