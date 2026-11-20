add_executable(iso-flash-write src/writer.cpp src/device_guard.cpp src/device_identity.cpp)
target_compile_options(iso-flash-write PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(GIO_UNIX REQUIRED IMPORTED_TARGET gio-unix-2.0)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
add_executable(iso-flash-service src/dbus_service.cpp src/device_guard.cpp src/device_identity.cpp)
target_link_libraries(iso-flash-service PRIVATE PkgConfig::GIO PkgConfig::GIO_UNIX PkgConfig::GLIB)
target_compile_options(iso-flash-service PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

if(BUILD_TESTING)
  add_executable(iso-flash-service-loop-test src/dbus_service.cpp src/device_guard.cpp src/device_identity.cpp)
  target_compile_definitions(iso-flash-service-loop-test PRIVATE ISO_FLASH_ENABLE_WRITES=1 ISO_FLASH_TEST_ALLOW_LOOP_TARGETS=1 ISO_FLASH_TEST_WRITE_DELAY=1)
  target_link_libraries(iso-flash-service-loop-test PRIVATE PkgConfig::GIO PkgConfig::GIO_UNIX PkgConfig::GLIB)
  target_compile_options(iso-flash-service-loop-test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

install(TARGETS iso-flash-write iso-flash-service RUNTIME DESTINATION lib/iso-flash)
