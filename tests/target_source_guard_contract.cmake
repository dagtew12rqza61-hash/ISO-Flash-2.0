if(NOT DEFINED TARGET_SOURCE OR NOT EXISTS "${TARGET_SOURCE}")
  message(FATAL_ERROR "Target source header is missing")
endif()

file(READ "${TARGET_SOURCE}" target_source)
string(REGEX MATCHALL "validate_device\\(" validations "${target_source}")
list(LENGTH validations validation_count)
if(NOT validation_count EQUAL 3)
  message(FATAL_ERROR "Target admission must run DeviceGuard before open, after descriptor admission, and during final revalidation")
endif()

if(NOT target_source MATCHES "validate_target_device\\(device_number")
  message(FATAL_ERROR "Default target admission must use the full DeviceGuard validator")
endif()

if(NOT target_source MATCHES "same_writable_device\\(admitted, current\\)")
  message(FATAL_ERROR "Target descriptor snapshot must be revalidated before admission")
endif()
