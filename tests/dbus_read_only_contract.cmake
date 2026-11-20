if(NOT DEFINED SERVICE_SOURCE OR NOT EXISTS "${SERVICE_SOURCE}")
  message(FATAL_ERROR "D-Bus service source is missing")
endif()

file(READ "${SERVICE_SOURCE}" service_source)
string(REGEX MATCHALL "<method name=\"[A-Za-z0-9_]+\"" methods "${service_source}")
list(LENGTH methods method_count)
if(NOT method_count EQUAL 5)
  message(FATAL_ERROR "The service must expose inspection, validation, authorization, StartWrite and CancelWrite methods")
endif()

foreach(required_method InspectTarget ValidateImage CheckWriteAuthorization StartWrite CancelWrite)
  if(NOT "${methods}" MATCHES "${required_method}")
    message(FATAL_ERROR "Missing D-Bus method: ${required_method}")
  endif()
endforeach()

if(NOT service_source MATCHES "inspect_target_read_only" OR
   NOT service_source MATCHES "validate_image_fd")
  message(FATAL_ERROR "Read-only inspection and image validation must call their validated helpers")
endif()

if(NOT service_source MATCHES "#ifndef ISO_FLASH_ENABLE_WRITES" OR
   NOT service_source MATCHES "WRITE_DISABLED")
  message(FATAL_ERROR "Production StartWrite must remain fail-closed unless explicitly enabled")
endif()

if(NOT service_source MATCHES "g_unix_fd_list_get" OR
   NOT service_source MATCHES "authorize_dbus_sender" OR
   NOT service_source MATCHES "open_writable_target_source" OR
   NOT service_source MATCHES "revalidate_writable_target_before_first_write")
  message(FATAL_ERROR "StartWrite must validate the received ISO FD, authorize the caller, and revalidate the target after authorization")
endif()

if(NOT service_source MATCHES "g_write_jobs.add" OR
   NOT service_source MATCHES "g_write_jobs.cancel" OR
   NOT service_source MATCHES "cancel_owner" OR
   NOT service_source MATCHES "g_dbus_connection_emit_signal" OR
   NOT service_source MATCHES "Progress" OR
   NOT service_source MATCHES "Finished")
  message(FATAL_ERROR "Write jobs must be owner-bound, cancellable, and publish progress/completion signals")
endif()

if(NOT service_source MATCHES "write_image_and_verify" OR
   NOT service_source MATCHES "g_task_run_in_thread")
  message(FATAL_ERROR "Write operations and descriptor validation must run outside the D-Bus main loop")
endif()

if(NOT service_source MATCHES "catch \\(const std::exception&" OR
   NOT service_source MATCHES "catch \\(...\\)")
  message(FATAL_ERROR "Asynchronous write admission must contain C++ exceptions")
endif()

if(NOT DEFINED TASK_HEADER OR NOT EXISTS "${TASK_HEADER}")
  message(FATAL_ERROR "Image validation task helper is missing")
endif()
file(READ "${TASK_HEADER}" task_source)
if(NOT task_source MATCHES "catch \\(const std::exception&" OR
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

if(NOT service_source MATCHES "subscribe_to_caller_disconnect" OR
   NOT service_source MATCHES "g_cancellable_is_cancelled" OR
   NOT service_source MATCHES "g_dbus_connection_signal_unsubscribe")
  message(FATAL_ERROR "Image hashing and StartWrite must be connected to the D-Bus caller lifecycle")
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
if(NOT polkit_source MATCHES "GetConnectionUnixProcessID" OR
   NOT polkit_source MATCHES "GetConnectionUnixUser" OR
   NOT polkit_source MATCHES "process_start_ticks" OR
   NOT polkit_source MATCHES "CheckAuthorization" OR
   NOT polkit_source MATCHES "system-bus-name" OR
   NOT polkit_source MATCHES "org.iso_flash.write")
  message(FATAL_ERROR "Polkit must authorize the unique D-Bus sender after binding UID, PID and process start time")
endif()

if(NOT DEFINED REGISTRY_HEADER OR NOT EXISTS "${REGISTRY_HEADER}")
  message(FATAL_ERROR "Owner-bound write-job registry is missing")
endif()
file(READ "${REGISTRY_HEADER}" registry_source)
if(NOT registry_source MATCHES "cancel\\(const std::string& owner" OR
   NOT registry_source MATCHES "cancel_owner" OR
   NOT registry_source MATCHES "snapshots")
  message(FATAL_ERROR "The write-job registry must enforce D-Bus caller ownership and support disconnect cancellation")
endif()

if(NOT DEFINED WRITE_OPERATION_HEADER OR NOT EXISTS "${WRITE_OPERATION_HEADER}")
  message(FATAL_ERROR "Verified write operation helper is missing")
endif()
file(READ "${WRITE_OPERATION_HEADER}" write_operation_source)
if(NOT write_operation_source MATCHES "fsync_function" OR
   NOT write_operation_source MATCHES "hash_fd_range_sha256" OR
   NOT write_operation_source MATCHES "EILSEQ" OR
   NOT write_operation_source MATCHES "revalidate_before_first_write")
  message(FATAL_ERROR "Write operation must sync, revalidate, and verify read-back SHA-256")
endif()
