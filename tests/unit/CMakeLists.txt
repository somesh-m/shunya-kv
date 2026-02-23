add_executable(cmd_set_unit_test
  cmd_set_test.cc
  ${SHUNYA_SRC_DIR}/cmd_set.cc
  ${SHUNYA_SRC_DIR}/router.cc
  ${SHUNYA_SRC_DIR}/kv_store.cc
  ${SHUNYA_SRC_DIR}/resp/resp_writer.cc
)

target_include_directories(cmd_set_unit_test PRIVATE
  ${SHUNYA_INCLUDE_DIR}
  ${SHUNYA_SRC_DIR}
  ${CMAKE_SOURCE_DIR}/third_party/fnv
)

target_link_libraries(cmd_set_unit_test PRIVATE
  Seastar::seastar_testing
  fmt::fmt
  absl::flat_hash_map
  fnv_ref
)

target_compile_definitions(cmd_set_unit_test PRIVATE
  SEASTAR_TESTING_MAIN
)

add_test(NAME SET_TEST_SET_VALID
  COMMAND cmd_set_unit_test --run_test=SET_TEST_SET_VALID
)

add_test(NAME SET_TEST_INVALID_ARGS
  COMMAND cmd_set_unit_test --run_test=SET_TEST_INVALID_ARGS
)

add_test(NAME SET_TEST_SHARD_HOPPING
  COMMAND cmd_set_unit_test --run_test=SET_TEST_SHARD_HOPPING)

add_test(NAME SET_TEST_OVERWRITE_EXISTING
  COMMAND cmd_set_unit_test --run_test=SET_TEST_OVERWRITE_EXISTING)

add_test(NAME SET_TEST_EMPTY_VALUE
  COMMAND cmd_set_unit_test --run_test=SET_TEST_EMPTY_VALUE)

add_test(NAME SET_TEST_LARGE_KEY
  COMMAND cmd_set_unit_test --run_test=SET_TEST_LARGE_KEY)

add_test(NAME SET_TEST_LARGE_VALUE
  COMMAND cmd_set_unit_test --run_test=SET_TEST_LARGE_VALUE)


add_executable(cmd_get_unit_test
  cmd_get_test.cc
  ${SHUNYA_SRC_DIR}/cmd_get.cc
  ${SHUNYA_SRC_DIR}/router.cc
  ${SHUNYA_SRC_DIR}/kv_store.cc
  ${SHUNYA_SRC_DIR}/resp/resp_writer.cc
)

target_include_directories(cmd_get_unit_test PRIVATE
  ${SHUNYA_INCLUDE_DIR}
  ${SHUNYA_SRC_DIR}
  ${CMAKE_SOURCE_DIR}/third_party/fnv
)

target_link_libraries(cmd_get_unit_test PRIVATE
  Seastar::seastar_testing
  fmt::fmt
  absl::flat_hash_map
  fnv_ref
)

target_compile_definitions(cmd_get_unit_test PRIVATE
  SEASTAR_TESTING_MAIN
)

add_test(NAME GET_TEST_VALID
  COMMAND cmd_get_unit_test --run_test=GET_TEST_VALID
)

add_test(NAME GET_TEST_INVALID_ARGS
  COMMAND cmd_get_unit_test --run_test=GET_TEST_INVALID_ARGS
)

add_test(NAME GET_TEST_SHARD_HOPPING
  COMMAND cmd_get_unit_test --run_test=GET_TEST_SHARD_HOPPING
)

add_test(NAME GET_TEST_OVERWRITE_EXISTING
  COMMAND cmd_get_unit_test --run_test=GET_TEST_OVERWRITE_EXISTING
)

add_test(NAME GET_TEST_LARGE_VALUE
  COMMAND cmd_get_unit_test --run_test=GET_TEST_LARGE_VALUE
)
