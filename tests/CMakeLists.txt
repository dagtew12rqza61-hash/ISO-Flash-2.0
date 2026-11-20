find_package(Threads REQUIRED)

add_test(
  NAME writer_refuses_destructive_operation
  COMMAND ${CMAKE_COMMAND}
    -DWRITER=$<TARGET_FILE:iso-flash-write>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/writer_disabled.cmake
)

add_test(
  NAME device_guard_rejects_non_block_target
  COMMAND ${CMAKE_COMMAND}
    -DWRITER=$<TARGET_FILE:iso-flash-write>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/guard_rejects_non_block.cmake
)

add_executable(mountinfo_parser_test mountinfo_parser.cpp)
target_include_directories(mountinfo_parser_test PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_options(mountinfo_parser_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
add_test(NAME mountinfo_parser_rejects_incomplete_metadata COMMAND mountinfo_parser_test)

add_executable(block_topology_test block_topology.cpp)
target_include_directories(block_topology_test PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_options(block_topology_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
add_test(NAME block_topology_detects_backing_and_fails_closed COMMAND block_topology_test)

add_executable(device_snapshot_test device_snapshot.cpp)
target_include_directories(device_snapshot_test PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_options(device_snapshot_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
add_test(NAME device_snapshot_rejects_replacement_and_capacity_changes COMMAND device_snapshot_test)

add_executable(device_io_test device_io.cpp)
target_include_directories(device_io_test PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_options(device_io_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
add_test(NAME device_io_handles_open_failures_and_closes_descriptors COMMAND device_io_test)

add_executable(device_error_test device_error.cpp)
target_include_directories(device_error_test PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_options(device_error_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
add_test(NAME device_error_reports_disconnect_and_media_state COMMAND device_error_test)

add_executable(write_job_state_test write_job_state.cpp)
target_include_directories(write_job_state_test PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_options(write_job_state_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
add_test(NAME write_job_state_enforces_safe_transitions COMMAND write_job_state_test)

add_executable(write_job_test write_job.cpp)
target_include_directories(write_job_test PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_options(write_job_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
add_test(NAME write_job_owns_descriptors_for_job_lifetime COMMAND write_job_test)

add_executable(write_io_test write_io.cpp)
target_include_directories(write_io_test PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_options(write_io_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
add_test(NAME write_io_handles_short_writes_errors_and_cancellation COMMAND write_io_test)

add_executable(write_progress_test write_progress.cpp)
target_include_directories(write_progress_test PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_options(write_progress_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
add_test(NAME write_progress_is_monotonic_and_bounded COMMAND write_progress_test)

add_executable(write_job_control_test write_job_control.cpp)
target_include_directories(write_job_control_test PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_options(write_job_control_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
target_link_libraries(write_job_control_test PRIVATE Threads::Threads)
add_test(NAME write_job_control_serializes_state_progress_and_cancellation COMMAND write_job_control_test)

add_executable(write_worker_test write_worker.cpp)
target_include_directories(write_worker_test PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_options(write_worker_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
target_link_libraries(write_worker_test PRIVATE Threads::Threads)
add_test(NAME write_worker_handles_async_progress_cancel_and_failures COMMAND write_worker_test)
