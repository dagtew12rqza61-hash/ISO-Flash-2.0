#pragma once

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace iso_flash {

struct ImageSnapshot {
    std::uint64_t device_number = 0;
    std::uint64_t inode_number = 0;
    std::uint64_t size_bytes = 0;
    std::int64_t modified_seconds = 0;
    std::int64_t modified_nanoseconds = 0;
    std::int64_t changed_seconds = 0;
    std::int64_t changed_nanoseconds = 0;
};

inline bool same_image_snapshot(const ImageSnapshot& original,
                                const ImageSnapshot& current) noexcept {
    return original.device_number == current.device_number &&
           original.inode_number == current.inode_number &&
           original.size_bytes > 0 &&
           original.size_bytes == current.size_bytes &&
           original.modified_seconds == current.modified_seconds &&
           original.modified_nanoseconds == current.modified_nanoseconds &&
           original.changed_seconds == current.changed_seconds &&
           original.changed_nanoseconds == current.changed_nanoseconds;
}

template <typename FstatFunction>
bool read_image_snapshot(int fd, ImageSnapshot& snapshot, std::string& error,
                         FstatFunction&& fstat_function) {
    snapshot = {};
    struct stat metadata {};
    int result = -1;
    do {
        result = fstat_function(fd, &metadata);
    } while (result != 0 && errno == EINTR);

    if (result != 0) {
        const int saved_error = errno;
        error = "Cannot inspect opened ISO descriptor (errno " +
                std::to_string(saved_error) + ")";
        return false;
    }
    if (!S_ISREG(metadata.st_mode)) {
        error = "ISO source must be a regular file";
        return false;
    }
    if (metadata.st_size <= 0) {
        error = "ISO source is empty";
        return false;
    }

    snapshot = ImageSnapshot{
        static_cast<std::uint64_t>(metadata.st_dev),
        static_cast<std::uint64_t>(metadata.st_ino),
        static_cast<std::uint64_t>(metadata.st_size),
        static_cast<std::int64_t>(metadata.st_mtim.tv_sec),
        static_cast<std::int64_t>(metadata.st_mtim.tv_nsec),
        static_cast<std::int64_t>(metadata.st_ctim.tv_sec),
        static_cast<std::int64_t>(metadata.st_ctim.tv_nsec)};
    return true;
}

template <typename OpenFunction, typename FstatFunction>
bool open_image_read_only(const std::string& path, int& fd,
                          ImageSnapshot& snapshot, std::string& error,
                          OpenFunction&& open_function,
                          FstatFunction&& fstat_function) {
    fd = -1;
    snapshot = {};
    int opened = -1;
    do {
        opened = open_function(path.c_str(),
                               O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    } while (opened < 0 && errno == EINTR);

    if (opened < 0) {
        const int saved_error = errno;
        error = "Cannot open ISO source (errno " + std::to_string(saved_error) + ")";
        return false;
    }

    if (!read_image_snapshot(opened, snapshot, error, fstat_function)) {
        const int saved_error = errno;
        (void)::close(opened);
        errno = saved_error;
        return false;
    }

    fd = opened;
    return true;
}

}  // namespace iso_flash
