if(NOT DEFINED SERVICE_SOURCE OR NOT EXISTS "${SERVICE_SOURCE}")
  message(FATAL_ERROR "D-Bus service source is missing")
endif()

file(READ "${SERVICE_SOURCE}" service_source)
string(REGEX MATCHALL "<method name=\"[A-Za-z0-9_]+\"" methods "${service_source}")
list(LENGTH methods method_count)
if(NOT method_count EQUAL 2)
  message(FATAL_ERROR "The privileged D-Bus service must expose exactly two read-only methods")
endif()

if(NOT "${methods}" MATCHES "InspectTarget" OR NOT "${methods}" MATCHES "ValidateImage")
  message(FATAL_ERROR "Only InspectTarget and ValidateImage may be exported")
endif()

if(NOT service_source MATCHES "inspect_target_read_only" OR
   NOT service_source MATCHES "validate_image_fd")
  message(FATAL_ERROR "Read-only methods must call their validated helpers")
endif()

if(service_source MATCHES "StartWrite|CancelWrite|WriteImage|O_RDWR|pwrite")
  message(FATAL_ERROR "The D-Bus service contains a forbidden write-capable endpoint or primitive")
endif()

if(NOT service_source MATCHES "g_task_run_in_thread")
  message(FATAL_ERROR "ValidateImage must hash asynchronously outside the D-Bus main loop")
endif()

if(NOT DEFINED LIMITER_HEADER OR NOT EXISTS "${LIMITER_HEADER}")
  message(FATAL_ERROR "Image validation limiter header is missing")
endif()
file(READ "${LIMITER_HEADER}" limiter_source)
if(NOT service_source MATCHES "ImageValidationLimiter" OR
   NOT service_source MATCHES "try_acquire" OR
   NOT service_source MATCHES "ImageValidationLimiter::Lease" OR
   NOT limiter_source MATCHES "compare_exchange_weak" OR
   NOT limiter_source MATCHES "fetch_sub" OR
   NOT limiter_source MATCHES "~Lease")
  message(FATAL_ERROR "D-Bus image validation must use the tested RAII concurrency limiter")
endif()

if(NOT service_source MATCHES "g_unix_fd_list_get")
  message(FATAL_ERROR "ValidateImage must receive the passed descriptor through GUnixFDList")
endif()
