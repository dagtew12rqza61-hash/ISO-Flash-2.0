add_executable(iso-flash-write src/writer.cpp src/device_guard.cpp src/device_identity.cpp)
target_compile_options(iso-flash-write PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
add_executable(iso-flash-service src/dbus_service.cpp src/device_guard.cpp src/device_identity.cpp)
target_link_libraries(iso-flash-service PRIVATE PkgConfig::GIO)
target_compile_options(iso-flash-service PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

install(TARGETS iso-flash-write iso-flash-service RUNTIME DESTINATION lib/iso-flash)
