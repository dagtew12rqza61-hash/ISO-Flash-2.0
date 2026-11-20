add_executable(iso-flash-write src/writer.cpp src/device_guard.cpp src/device_identity.cpp)
target_compile_options(iso-flash-write PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
install(TARGETS iso-flash-write RUNTIME DESTINATION lib/iso-flash)
