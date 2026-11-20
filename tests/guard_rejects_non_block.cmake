execute_process(
  COMMAND "${WRITER}" --check /dev/null
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 4)
  message(FATAL_ERROR "Expected target rejection exit code 4, got: ${result}")
endif()
if(NOT error MATCHES "TARGET_REJECTED")
  message(FATAL_ERROR "Guard did not reject non-block target: ${error}")
endif()
