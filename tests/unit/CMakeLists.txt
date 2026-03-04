add_executable(cmd_set_unit_test
  cmd_set_test.cc
  ${SHUNYA_SRC_DIR}/cmd_set.cc
  ${SHUNYA_SRC_DIR}/router.cc
  ${SHUNYA_SRC_DIR}/kv_store.cc
  ${SHUNYA_SRC_DIR}/pool/object_pool.cc
  ${SHUNYA_SRC_DIR}/eviction/sieve_policy.cc
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

add_test(NAME SET_TEST_TTL_VALID
  COMMAND cmd_set_unit_test --run_test=SET_TEST_TTL_VALID)

add_test(NAME SET_TEST_TTL_NON_NUMERIC_ARGS
  COMMAND cmd_set_unit_test --run_test=SET_TEST_TTL_NON_NUMERIC_ARGS)

add_test(NAME SET_TEST_TTL_REFRESH_ON_OVERWRITE
  COMMAND cmd_set_unit_test --run_test=SET_TEST_TTL_REFRESH_ON_OVERWRITE)

add_test(NAME SET_TEST_TTL_SET_WITHOUT_TTL_OVERWRITE
  COMMAND cmd_set_unit_test --run_test=SET_TEST_TTL_SET_WITHOUT_TTL_OVERWRITE)

add_executable(cmd_get_unit_test
  cmd_get_test.cc
  ${SHUNYA_SRC_DIR}/cmd_get.cc
  ${SHUNYA_SRC_DIR}/router.cc
  ${SHUNYA_SRC_DIR}/kv_store.cc
  ${SHUNYA_SRC_DIR}/pool/object_pool.cc
  ${SHUNYA_SRC_DIR}/eviction/sieve_policy.cc
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

add_test(NAME GET_TEST_INVALID_KEY
  COMMAND cmd_get_unit_test --run_test=GET_TEST_INVALID_KEY
)

add_test(NAME GET_TEST_TTL_VALID
  COMMAND cmd_get_unit_test --run_test=GET_TEST_TTL_VALID
)

add_executable(cmd_node_info_unit_test
  cmd_node_info_test.cc
  ${SHUNYA_SRC_DIR}/cmd_node_info.cc
  ${SHUNYA_SRC_DIR}/node_info_helpers.cc
  ${SHUNYA_SRC_DIR}/router.cc
  ${SHUNYA_SRC_DIR}/kv_store.cc
  ${SHUNYA_SRC_DIR}/pool/object_pool.cc
  ${SHUNYA_SRC_DIR}/eviction/sieve_policy.cc
  ${SHUNYA_SRC_DIR}/resp/resp_writer.cc
)

target_include_directories(cmd_node_info_unit_test PRIVATE
  ${SHUNYA_INCLUDE_DIR}
  ${SHUNYA_SRC_DIR}
  ${CMAKE_SOURCE_DIR}/third_party/fnv
)

target_link_libraries(cmd_node_info_unit_test PRIVATE
  Seastar::seastar_testing
  fmt::fmt
  absl::flat_hash_map
  fnv_ref
)

target_compile_definitions(cmd_node_info_unit_test PRIVATE
  SEASTAR_TESTING_MAIN
)

add_test(NAME NODE_INFO_MAP_TEST
  COMMAND cmd_node_info_unit_test --run_test=NODE_INFO_MAP_TEST
)

add_test(NAME NODE_INFO_CURRENT_CONN_TEST
  COMMAND cmd_node_info_unit_test --run_test=NODE_INFO_CURRENT_CONN_TEST
)

add_test(NAME NODE_INFO_OTHER_NODE_TEST
  COMMAND cmd_node_info_unit_test --run_test=NODE_INFO_OTHER_NODE_TEST
)

add_executable(sieve_policy_unit_test
  sieve_policy_test.cc
  ${SHUNYA_SRC_DIR}/eviction/sieve_policy.cc
)

target_include_directories(sieve_policy_unit_test PRIVATE
  ${SHUNYA_INCLUDE_DIR}
  ${SHUNYA_SRC_DIR}
  ${CMAKE_SOURCE_DIR}/third_party/fnv
)

target_link_libraries(sieve_policy_unit_test PRIVATE
  Seastar::seastar_testing
  fmt::fmt
  absl::flat_hash_map
  fnv_ref
)

target_compile_definitions(sieve_policy_unit_test PRIVATE
  SEASTAR_TESTING_MAIN
)

add_test(NAME SIEVE_POLICY_EVICTION_LOWER_BUDGET
  COMMAND sieve_policy_unit_test --run_test=sieve_policy_eviction_lower_budget
)

add_test(NAME SIEVE_POLICY_EVICTION_HIGHER_BUDGET
  COMMAND sieve_policy_unit_test --run_test=sieve_policy_eviction_higher_budget
)

add_test(NAME SIEVE_POLICY_NO_ENTRY
  COMMAND sieve_policy_unit_test --run_test=sieve_policy_no_entry
)

add_test(NAME SIEVE_POLICY_WITH_KEYS_DELETION_SCENARIO_I
  COMMAND sieve_policy_unit_test --run_test=sieve_policy_eviction_with_keys_deletion_scenario_I
)

add_test(NAME SIEVE_POLICY_WITH_KEYS_DELETION_SCENARIO_II
  COMMAND sieve_policy_unit_test --run_test=sieve_policy_with_keys_deletion_scenario_II
)

add_test(NAME SIEVE_POLICY_NO_EVICTION
  COMMAND sieve_policy_unit_test --run_test=sieve_policy_no_eviction
)

add_test(NAME SIEVE_POLICY_HAND_WRAP_AROUND
  COMMAND sieve_policy_unit_test --run_test=sieve_policy_hand_wrap_around
)

add_executable(object_pool_unit_test
  object_pool_test.cc
  ${SHUNYA_SRC_DIR}/pool/object_pool.cc
)

target_include_directories(object_pool_unit_test PRIVATE
  ${SHUNYA_INCLUDE_DIR}
  ${SHUNYA_SRC_DIR}
  ${CMAKE_SOURCE_DIR}/third_party/fnv
)

target_link_libraries(object_pool_unit_test PRIVATE
  Seastar::seastar_testing
  fmt::fmt
  absl::flat_hash_map
  fnv_ref
)

target_compile_definitions(object_pool_unit_test PRIVATE
  SEASTAR_TESTING_MAIN
)

add_test(NAME OBJECT_POOL_CREATE_BY_MEMORY
  COMMAND object_pool_unit_test --run_test=object_pool_create_by_memory
)

add_test(NAME OBJECT_POOL_CREATE_FIXED_SIZE
  COMMAND object_pool_unit_test --run_test=object_pool_fixed_size
)

add_test(NAME OBJECT_POOL_CREATE_OVERFLOW
  COMMAND object_pool_unit_test --run_test=object_pool_create_overflow
)

add_test(NAME OBJECT_POOL_ACQUIRE_RELEASE
  COMMAND object_pool_unit_test --run_test=object_pool_acquire_release
)

add_test(NAME OBJECT_POOL_MULTIPLE_INIT
  COMMAND object_pool_unit_test --run_test=object_pool_multiple_init
)

add_test(NAME OBJECT_POOL_ACQUIRE_OVERFLOW
  COMMAND object_pool_unit_test --run_test=object_pool_acquire_overflow
)

add_test(NAME OBJECT_POOL_RELEASE_OVERFLOW
  COMMAND object_pool_unit_test --run_test=object_pool_release_overflow
)

add_test(NAME OBJECT_POOL_MUTATE_FIELDS
  COMMAND object_pool_unit_test --run_test=object_pool_mutate_fields
)

add_test(NAME OBJECT_POOL_DISTINCT_POINTER
  COMMAND object_pool_unit_test --run_test=object_pool_distinct_pointer
)
