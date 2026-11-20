#include "device_error.hpp"

#include <cerrno>
#include <iostream>
#include <string>

int main() {
    const auto check = [](int code, const std::string& expected) {
        const std::string actual = iso_flash::describe_device_error("Device operation failed", code);
        if (actual.find(expected) == std::string::npos ||
            actual.find("errno=" + std::to_string(code)) == std::string::npos) {
            std::cerr << "Unexpected diagnostic for errno " << code << ": " << actual << '\\n';
            return false;
        }
        return true;
    };

    if (!check(ENODEV, "disconnected or is no longer available") ||
        !check(ENXIO, "device is no longer present") ||
        !check(EIO, "device I/O error") ||
        !check(EBUSY, "device is busy") ||
        !check(EACCES, "permission denied") ||
        !check(EPERM, "permission denied") ||
        !check(EINTR, "operation was interrupted") ||
        !check(ENOMEDIUM, "no media is present") ||
        !check(EROFS, "device became read-only")) {
        return 1;
    }
    return 0;
}
