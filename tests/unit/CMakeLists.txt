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
  ${SHUNYA_SRC_DIR}/eviction/sieve_policy.cc
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

add_executable(orchestrator_unit_test
  orchestrator_test.cc
  ${SHUNYA_SRC_DIR}/conn/orchestrator.cc
  ${SHUNYA_SRC_DIR}/conn/socket_server.cc
  ${SHUNYA_SRC_DIR}/conn/socket_handler.cc
  ${SHUNYA_SRC_DIR}/conn/resp_handler.cc
  ${SHUNYA_SRC_DIR}/conn/connections.cc
  ${SHUNYA_SRC_DIR}/resp/resp_stream_processor.cc
  ${SHUNYA_SRC_DIR}/commands.cc
  ${SHUNYA_SRC_DIR}/cmd_set.cc
  ${SHUNYA_SRC_DIR}/cmd_get.cc
  ${SHUNYA_SRC_DIR}/cmd_info.cc
  ${SHUNYA_SRC_DIR}/cmd_quit.cc
  ${SHUNYA_SRC_DIR}/cmd_node_info.cc
  ${SHUNYA_SRC_DIR}/node_info_helpers.cc
  ${SHUNYA_SRC_DIR}/router.cc
  ${SHUNYA_SRC_DIR}/kv_store.cc
  ${SHUNYA_SRC_DIR}/pool/object_pool.cc
  ${SHUNYA_SRC_DIR}/eviction/sieve_policy.cc
  ${SHUNYA_SRC_DIR}/resp/resp_writer.cc
)

target_include_directories(orchestrator_unit_test PRIVATE
  ${SHUNYA_INCLUDE_DIR}
  ${SHUNYA_SRC_DIR}
  ${CMAKE_SOURCE_DIR}/third_party/fnv
)

target_link_libraries(orchestrator_unit_test PRIVATE
  Seastar::seastar_testing
  fmt::fmt
  absl::flat_hash_map
  fnv_ref
)

target_compile_definitions(orchestrator_unit_test PRIVATE
  SEASTAR_TESTING_MAIN
)

add_test(NAME ORCHESTRATOR_START_STOP_HAPPY_PATH
  COMMAND orchestrator_unit_test --run_test=ORCHESTRATOR_START_STOP_HAPPY_PATH
)

add_test(NAME ORCHESTRATOR_SET_HANDLER_AND_SERVE_REQUESTS
  COMMAND orchestrator_unit_test --run_test=ORCHESTRATOR_SET_HANDLER_AND_SERVE_REQUESTS
)

add_test(NAME ORCHESTRATOR_MULTIPLE_START_STOP
  COMMAND orchestrator_unit_test --run_test=orchestrator_multiple_start_stop
)

add_test(NAME ORCHESTRATOR_PROTOCOL_SANITY
  COMMAND orchestrator_unit_test --run_test=orchestrator_protocol_sanity
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

add_test(NAME OBJECT_POOL_PROMOTE_TO_SANCTUARY_TRACKS_SIEVE
  COMMAND object_pool_unit_test --run_test=object_pool_promote_to_sanctuary_tracks_sieve
)

add_test(NAME OBJECT_POOL_REUSED_ENTRY_RELINKS_SANCTUARY_WITHOUT_PROBATION
  COMMAND object_pool_unit_test --run_test=object_pool_reused_entry_relinks_into_sanctuary_when_probation_disabled
)


add_executable(resp_handler_unit_test
  resp_stream_extractor_test.cc
  resp_request_test.cc
  ${SHUNYA_SRC_DIR}/conn/resp_handler.cc
  ${SHUNYA_SRC_DIR}/conn/socket_handler.cc
  ${SHUNYA_SRC_DIR}/resp/resp_stream_processor.cc
  ${SHUNYA_SRC_DIR}/commands.cc
  ${SHUNYA_SRC_DIR}/cmd_set.cc
  ${SHUNYA_SRC_DIR}/cmd_get.cc
  ${SHUNYA_SRC_DIR}/cmd_info.cc
  ${SHUNYA_SRC_DIR}/cmd_quit.cc
  ${SHUNYA_SRC_DIR}/cmd_node_info.cc
  ${SHUNYA_SRC_DIR}/node_info_helpers.cc
  ${SHUNYA_SRC_DIR}/router.cc
  ${SHUNYA_SRC_DIR}/kv_store.cc
  ${SHUNYA_SRC_DIR}/pool/object_pool.cc
  ${SHUNYA_SRC_DIR}/eviction/sieve_policy.cc
  ${SHUNYA_SRC_DIR}/resp/resp_writer.cc
)

target_include_directories(resp_handler_unit_test PRIVATE
  ${SHUNYA_INCLUDE_DIR}
  ${SHUNYA_SRC_DIR}
  ${CMAKE_SOURCE_DIR}/third_party/fnv
)

target_link_libraries(resp_handler_unit_test PRIVATE
  Seastar::seastar_testing
  fmt::fmt
  absl::flat_hash_map
  fnv_ref
)

target_compile_definitions(resp_handler_unit_test PRIVATE
  SEASTAR_TESTING_MAIN
)

add_test(NAME RESP_PARSER_SET_COMMAND_INCREMENTAL
  COMMAND resp_handler_unit_test --run_test=resp_parser_set_command_incremental
)

add_test(NAME RESP_PARSER_GET_COMMAND_INCREMENTAL
  COMMAND resp_handler_unit_test --run_test=resp_processor_get_command_incremental
)

add_test(NAME RESP_PARSER_SET_COMMAND
  COMMAND resp_handler_unit_test --run_test=resp_processor_set_command
)

add_test(NAME RESP_PARSER_GET_COMMAND
  COMMAND resp_handler_unit_test --run_test=resp_processor_get_command
)

add_test(NAME RESP_PARSER_INVALID_COMMAND_START_SEQUENCE
  COMMAND resp_handler_unit_test --run_test=resp_processor_invalid_command_start_sequence
)

add_test(NAME RESP_PARSER_INVALID_ARG_COUNT_OVERFLOW
  COMMAND resp_handler_unit_test --run_test=resp_processor_invalid_arg_count_overflow
)

add_test(NAME RESP_PARSER_INVALID_ARG_COUNT_UNDERFLOW
  COMMAND resp_handler_unit_test --run_test=resp_processor_invalid_arg_count_underflow
)

add_test(NAME RESP_PARSER_MAX_ARG_COUNT
  COMMAND resp_handler_unit_test --run_test=resp_processor_max_arg_count
)

add_test(NAME RESP_PARSER_MIN_ARG_COUNT
  COMMAND resp_handler_unit_test --run_test=resp_processor_min_arg_count
)

add_test(NAME RESP_PARSER_COMMAND_PIPELINED
  COMMAND resp_handler_unit_test --run_test=resp_processor_command_pipelined
)

add_test(NAME RESP_PARSER_COMMAND_PIPELINED_SPLIT
  COMMAND resp_handler_unit_test --run_test=resp_processor_command_pipelined_split
)

add_test(NAME RESP_PARSER_COMMAND_PIPELINED_VALID_INVALID
  COMMAND resp_handler_unit_test --run_test=resp_processor_command_pipelined_valid_invalid
)

add_test(NAME RESP_PARSER_BLANK_COMMAND
  COMMAND resp_handler_unit_test --run_test=resp_processor_blank_command
)

add_test(NAME RESP_PARSER_STRESS
  COMMAND resp_handler_unit_test --run_test=resp_processor_stress
)

add_test(NAME RESP_PARSER_MISSING_CRLF
  COMMAND resp_handler_unit_test --run_test=resp_processor_missing_crlf
)

add_test(NAME RESP_REQUEST_HANDLER_EMPTY_COMMAND
  COMMAND resp_handler_unit_test --run_test=resp_request_handler_empty_command
)

add_test(NAME RESP_REQUEST_HANDLER_ERROR_COMMAND
  COMMAND resp_handler_unit_test --run_test=resp_request_handler_error_command
)

add_test(NAME RESP_REQUEST_HANDLER_INVALID_COMMAND
  COMMAND resp_handler_unit_test --run_test=resp_request_handler_invalid_command
)

add_executable(socket_handler_unit_test
  socket_handler_test.cc
  ${SHUNYA_SRC_DIR}/conn/socket_handler.cc
  ${SHUNYA_SRC_DIR}/cmd_node_info.cc
  ${SHUNYA_SRC_DIR}/node_info_helpers.cc
  ${SHUNYA_SRC_DIR}/pool/object_pool.cc
  ${SHUNYA_SRC_DIR}/eviction/sieve_policy.cc
  ${SHUNYA_SRC_DIR}/resp/resp_writer.cc
)

target_include_directories(socket_handler_unit_test PRIVATE
  ${SHUNYA_INCLUDE_DIR}
  ${SHUNYA_SRC_DIR}
  ${CMAKE_SOURCE_DIR}/third_party/fnv
)

target_link_libraries(socket_handler_unit_test PRIVATE
  Seastar::seastar_testing
  fmt::fmt
  absl::flat_hash_map
  fnv_ref
)

target_compile_definitions(socket_handler_unit_test PRIVATE
  SEASTAR_TESTING_MAIN
)

add_test(NAME SOCKET_HANDLER_PIPELINING_SEMANTICS
  COMMAND socket_handler_unit_test --run_test=socket_handler_pipelining_semantics
)

add_test(NAME SOCKET_HANDLER_BACKPRESSURE_TEST
  COMMAND socket_handler_unit_test --run_test=socket_handler_backpressure_test
)

add_test(NAME SOCKET_HANDLER_PENDING_QUEUE
  COMMAND socket_handler_unit_test --run_test=socket_handler_pending_queue
)

add_test(NAME SOCKET_HANDLER_STUCK_HANDLER
  COMMAND socket_handler_unit_test --run_test=socket_handler_stuck_handler
)
