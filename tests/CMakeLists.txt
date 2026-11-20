find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(GIO_UNIX REQUIRED IMPORTED_TARGET gio-unix-2.0)

add_executable(test_dbus_fd_client dbus_fd_client.cpp)
target_link_libraries(test_dbus_fd_client PRIVATE PkgConfig::GIO PkgConfig::GIO_UNIX)
target_compile_options(test_dbus_fd_client PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

add_executable(test_polkit_dbus_integration polkit_dbus_integration.cpp)
add_executable(test_polkit_real_integration polkit_real_integration.cpp)
target_link_libraries(test_polkit_real_integration PRIVATE PkgConfig::GIO)
target_compile_options(test_polkit_real_integration PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
target_link_libraries(test_polkit_dbus_integration PRIVATE PkgConfig::GIO Threads::Threads)
target_compile_options(test_polkit_dbus_integration PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

add_executable(test_dbus_caller_watch dbus_caller_watch.cpp)
target_include_directories(test_dbus_caller_watch PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_link_libraries(test_dbus_caller_watch PRIVATE PkgConfig::GIO)
target_compile_options(test_dbus_caller_watch PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

set(ISO_FLASH_TESTS
  block_topology
  device_error
  device_io
  device_snapshot
  image_fd_validation
  image_hash
  image_source
  image_validation
  image_validation_limiter
  image_validation_task
  mountinfo_parser
  opened_image_job
  opened_target_job
  write_io
  write_operation
  write_job
  write_job_control
  write_job_registry
  write_job_state
  write_progress
  write_worker
  writable_target
  swapinfo_parser
  polkit_authorization
)

foreach(test_name IN LISTS ISO_FLASH_TESTS)
  add_executable(test_${test_name} ${test_name}.cpp)
  target_include_directories(test_${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
  target_link_libraries(test_${test_name} PRIVATE Threads::Threads)
  target_compile_options(test_${test_name} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  if(test_name STREQUAL "polkit_authorization")
    target_link_libraries(test_${test_name} PRIVATE PkgConfig::GIO)
  endif()
  if(test_name STREQUAL "write_operation" OR test_name STREQUAL "image_hash" OR test_name STREQUAL "image_validation" OR test_name STREQUAL "image_fd_validation" OR test_name STREQUAL "image_validation_task")
    target_link_libraries(test_${test_name} PRIVATE PkgConfig::GLIB)
  endif()
  add_test(NAME ${test_name} COMMAND test_${test_name})
endforeach()

add_test(
  NAME opened_target_admission_repeats_full_device_guard
  COMMAND ${CMAKE_COMMAND}
    -DTARGET_SOURCE=${CMAKE_SOURCE_DIR}/engine/src/target_source.hpp
    -P ${CMAKE_CURRENT_SOURCE_DIR}/target_source_guard_contract.cmake
)

add_test(
  NAME dbus_read_only_service_contract
  COMMAND ${CMAKE_COMMAND}
    -DSERVICE_SOURCE=${CMAKE_SOURCE_DIR}/engine/src/dbus_service.cpp
    -DLIMITER_HEADER=${CMAKE_SOURCE_DIR}/engine/src/image_validation_limiter.hpp
    -DTASK_HEADER=${CMAKE_SOURCE_DIR}/engine/src/image_validation_task.hpp
    -DCALLER_WATCH_HEADER=${CMAKE_SOURCE_DIR}/engine/src/dbus_caller_watch.hpp
    -DPOLKIT_HEADER=${CMAKE_SOURCE_DIR}/engine/src/polkit_authorization.hpp
    -P ${CMAKE_CURRENT_SOURCE_DIR}/dbus_read_only_contract.cmake
)

add_test(
  NAME systemd_service_hardening_contract
  COMMAND ${CMAKE_COMMAND}
    -DSERVICE_FILE=${CMAKE_SOURCE_DIR}/packaging/systemd/iso-flash.service
    -DPOLKIT_POLICY=${CMAKE_SOURCE_DIR}/packaging/polkit/org.iso_flash.policy
    -P ${CMAKE_CURRENT_SOURCE_DIR}/systemd_service_contract.cmake
)
