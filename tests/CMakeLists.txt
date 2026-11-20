if(NOT DEFINED SERVICE_SOURCE OR NOT EXISTS "${SERVICE_SOURCE}")
  message(FATAL_ERROR "D-Bus service source is missing")
endif()

file(READ "${SERVICE_SOURCE}" service_source)
string(REGEX MATCHALL "<method name=\"[A-Za-z0-9_]+\"" methods "${service_source}")
list(LENGTH methods method_count)
if(NOT method_count EQUAL 1)
  message(FATAL_ERROR "The privileged D-Bus service must expose exactly one method")
endif()

if(NOT "${methods}" STREQUAL "<method name=\"InspectTarget\"")
  message(FATAL_ERROR "The only exported D-Bus method must be InspectTarget")
endif()

if(NOT service_source MATCHES "inspect_target_read_only")
  message(FATAL_ERROR "InspectTarget must use the read-only target inspection path")
endif()

if(service_source MATCHES "StartWrite|CancelWrite|WriteImage|O_RDWR|pwrite")
  message(FATAL_ERROR "The D-Bus service contains a forbidden write-capable endpoint or primitive")
endif()

add_test(
  NAME opened_target_admission_repeats_full_device_guard
  COMMAND ${CMAKE_COMMAND}
    -DTARGET_SOURCE=${CMAKE_SOURCE_DIR}/engine/src/target_source.hpp
    -P ${CMAKE_CURRENT_SOURCE_DIR}/target_source_guard_contract.cmake
)
