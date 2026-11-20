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
