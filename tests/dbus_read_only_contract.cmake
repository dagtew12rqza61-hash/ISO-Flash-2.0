if(NOT DEFINED SERVICE_SOURCE OR NOT EXISTS "${SERVICE_SOURCE}")
  message(FATAL_ERROR "D-Bus service source is missing")
endif()

file(READ "${SERVICE_SOURCE}" service_source)
string(REGEX MATCHALL "<method name=\"[A-Za-z0-9_]+\"" methods "${service_source}")
list(LENGTH methods method_count)
if(NOT method_count EQUAL 3)
  message(FATAL_ERROR "The service must expose two read-only methods and one Polkit authorization method")
endif()

if(NOT "${methods}" MATCHES "InspectTarget" OR
   NOT "${methods}" MATCHES "ValidateImage" OR
   NOT "${methods}" MATCHES "CheckWriteAuthorization")
  message(FATAL_ERROR "Unexpected D-Bus inspection or authorization interface")
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

if(NOT DEFINED TASK_HEADER OR NOT EXISTS "${TASK_HEADER}")
  message(FATAL_ERROR "Image validation task helper is missing")
endif()
file(READ "${TASK_HEADER}" task_source)
if(NOT service_source MATCHES "run_image_validation_task" OR
   NOT task_source MATCHES "catch \\(const std::exception&" OR
   NOT task_source MATCHES "catch \\(\\.\\.\\.\\)" OR
   NOT task_source MATCHES "Image validation failed with an unknown exception")
  message(FATAL_ERROR "C++ exceptions must be contained by the tested image validation task helper")
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

if(NOT service_source MATCHES "subscribe_to_caller_disconnect" OR
   NOT service_source MATCHES "g_cancellable_is_cancelled" OR
   NOT service_source MATCHES "g_dbus_connection_signal_unsubscribe")
  message(FATAL_ERROR "Image hashing must be connected to the tested D-Bus caller lifecycle")
endif()

if(NOT DEFINED CALLER_WATCH_HEADER OR NOT EXISTS "${CALLER_WATCH_HEADER}")
  message(FATAL_ERROR "D-Bus caller disconnect watcher header is missing")
endif()
file(READ "${CALLER_WATCH_HEADER}" caller_watch_source)
if(NOT caller_watch_source MATCHES "NameOwnerChanged" OR
   NOT caller_watch_source MATCHES "g_cancellable_cancel" OR
   NOT caller_watch_source MATCHES "g_dbus_connection_signal_subscribe")
  message(FATAL_ERROR "Caller disconnect watcher must subscribe to NameOwnerChanged and cancel its task")
endif()

if(NOT DEFINED POLKIT_HEADER OR NOT EXISTS "${POLKIT_HEADER}")
  message(FATAL_ERROR "D-Bus caller and Polkit authorization helper is missing")
endif()
file(READ "${POLKIT_HEADER}" polkit_source)
if(NOT service_source MATCHES "authorize_dbus_sender" OR
   NOT service_source MATCHES "subscribe_to_caller_disconnect" OR
   NOT polkit_source MATCHES "GetConnectionUnixProcessID" OR
   NOT polkit_source MATCHES "GetConnectionUnixUser" OR
   NOT polkit_source MATCHES "process_start_ticks" OR
   NOT polkit_source MATCHES "CheckAuthorization" OR
   NOT polkit_source MATCHES "system-bus-name" OR
   NOT polkit_source MATCHES "org.iso_flash.write")
  message(FATAL_ERROR "Polkit must authorize the unique D-Bus sender after binding UID, PID and process start time")
endif()
