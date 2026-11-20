#pragma once

#include <cerrno>
#include <cstdint>
#include <string>
#include <linux/fs.h>
#include <sys/stat.h>

namespace iso_flash {

struct DeviceSnapshot {
    std::uint64_t device_number;
    std::uint64_t capacity_bytes;
    bool read_only;
};

inline bool same_writable_device(const DeviceSnapshot& original,
                                 const DeviceSnapshot& current) {
    return original.device_number == current.device_number &&
           original.capacity_bytes > 0 &&
           original.capacity_bytes == current.capacity_bytes &&
           !original.read_only && !current.read_only;
}

inline bool same_opened_target(const DeviceSnapshot& original,
                               std::uint64_t path_device_number,
                               const DeviceSnapshot& opened_current) {
    return original.device_number == path_device_number &&
           same_writable_device(original, opened_current);
}

inline bool matches_expected_device(const DeviceSnapshot& opened,
                                    std::uint64_t expected_device_number) {
    return expected_device_number == 0 ||
           opened.device_number == expected_device_number;
}

template <typename FstatFunction, typename IoctlFunction>
bool read_device_snapshot(int fd, DeviceSnapshot& snapshot, std::string& error,
                          FstatFunction&& fstat_function,
                          IoctlFunction&& ioctl_function) {
    struct stat metadata{};
    if (fstat_function(fd, &metadata) != 0 || !S_ISBLK(metadata.st_mode)) {
        error = "Cannot read opened block device metadata";
        return false;
    }

    std::uint64_t capacity_bytes = 0;
    int read_only = 1;
    if (ioctl_function(fd, BLKGETSIZE64, &capacity_bytes) != 0 ||
        ioctl_function(fd, BLKROGET, &read_only) != 0) {
        error = "Cannot read block device capacity or read-only state";
        return false;
    }
    if (capacity_bytes == 0) {
        error = "Target has zero capacity";
        return false;
    }

    snapshot = DeviceSnapshot{
        static_cast<std::uint64_t>(metadata.st_rdev), capacity_bytes, read_only != 0};
    return true;
}

}  // namespace iso_flash
