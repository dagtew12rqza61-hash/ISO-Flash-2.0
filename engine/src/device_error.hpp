#pragma once

#include <cerrno>
#include <string>

namespace iso_flash {

inline std::string describe_device_error(const char* operation, int error_number) {
    const char* reason = "unspecified system error";
    switch (error_number) {
        case ENODEV:
            reason = "device was disconnected or is no longer available";
            break;
        case ENXIO:
            reason = "device is no longer present";
            break;
        case EIO:
            reason = "device I/O error";
            break;
        case EBUSY:
            reason = "device is busy";
            break;
        case ENOMEDIUM:
            reason = "no media is present";
            break;
        case EROFS:
            reason = "device became read-only";
            break;
        case EACCES:
        case EPERM:
            reason = "permission denied";
            break;
        case EINTR:
            reason = "operation was interrupted";
            break;
        default:
            break;
    }
    return std::string(operation) + ": " + reason +
           " (errno=" + std::to_string(error_number) + ")";
}

}  // namespace iso_flash
