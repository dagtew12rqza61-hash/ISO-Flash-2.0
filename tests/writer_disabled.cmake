execute_process(
  COMMAND "${WRITER}" --write /tmp/not-used.iso /dev/not-a-real-device
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 3)
  message(FATAL_ERROR "Expected writer exit code 3, got: ${result}")
endif()
if(NOT error MATCHES "WRITE_DISABLED")
  message(FATAL_ERROR "Writer did not report WRITE_DISABLED: ${error}")
endif()
