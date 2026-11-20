#pragma once

#include "device_io.hpp"
#include "image_hash.hpp"

#include <cerrno>
#include <fcntl.h>
#include <string>

namespace iso_flash {

struct ImageFdValidationResult {
    bool valid = false;
    ImageSnapshot snapshot;
    std::string sha256;
    std::uint64_t bytes_hashed = 0;
    int error_number = 0;
    std::string reason;
};

inline ImageFdValidationResult validate_image_fd(int fd) {
    ImageFdValidationResult result;
    if (fd < 0) {
        result.error_number = EBADF;
        result.reason = "Invalid image descriptor";
        return result;
    }

    int flags = -1;
    do {
        flags = ::fcntl(fd, F_GETFL);
    } while (flags < 0 && errno == EINTR);
    if (flags < 0) {
        result.error_number = errno;
        result.reason = "Cannot inspect image descriptor flags";
        return result;
    }
    if ((flags & O_ACCMODE) != O_RDONLY) {
        result.error_number = EACCES;
        result.reason = "Image descriptor must be opened read-only";
        return result;
    }

    if (!read_image_snapshot(
            fd, result.snapshot, result.reason,
            [](int descriptor, struct stat* metadata) {
                return ::fstat(descriptor, metadata);
            })) {
        result.error_number = errno == 0 ? EINVAL : errno;
        return result;
    }

    const ImageHashResult hash =
        hash_image_sha256(fd, result.snapshot, [] { return false; });
    result.bytes_hashed = hash.bytes_hashed;
    if (hash.status != ImageHashStatus::Completed) {
        result.error_number = hash.error_number == 0 ? EIO : hash.error_number;
        result.reason = hash.error.empty() ? "Image SHA-256 validation failed" : hash.error;
        return result;
    }

    result.sha256 = hash.sha256;
    result.valid = true;
    return result;
}

}  // namespace iso_flash
