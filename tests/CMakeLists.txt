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
