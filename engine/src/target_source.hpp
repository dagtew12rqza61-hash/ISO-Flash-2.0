#pragma once

#include "device_io.hpp"
#include "device_snapshot.hpp"
#include "device_guard.hpp"

#include <cerrno>
#include <cstdint>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace iso_flash {

struct OpenedTarget {
    UniqueFd descriptor;
    DeviceSnapshot snapshot;

    OpenedTarget() = default;
    OpenedTarget(UniqueFd opened_descriptor, DeviceSnapshot opened_snapshot) noexcept
        : descriptor(std::move(opened_descriptor)), snapshot(opened_snapshot) {}

    OpenedTarget(const OpenedTarget&) = delete;
    OpenedTarget& operator=(const OpenedTarget&) = delete;
    OpenedTarget(OpenedTarget&&) noexcept = default;
    OpenedTarget& operator=(OpenedTarget&&) noexcept = default;

    bool valid() const noexcept {
        return descriptor.get() >= 0 && snapshot.capacity_bytes > 0 &&
               !snapshot.read_only && snapshot.device_number != 0;
    }
};

inline bool same_target_path_identity(const std::string& path,
                                      std::uint64_t expected_device_number,
                                      std::string& error) {
    struct stat metadata {};
    if (::stat(path.c_str(), &metadata) != 0) {
        const int saved_error = errno;
        error = describe_device_error("Cannot revalidate target path", saved_error);
        return false;
    }
    if (!S_ISBLK(metadata.st_mode)) {
        error = "Target path no longer resolves to a block device";
        return false;
    }
    if (static_cast<std::uint64_t>(metadata.st_rdev) != expected_device_number) {
        error = "Target path no longer resolves to the admitted device identity";
        return false;
    }
    return true;
}

template <typename ValidateFunction>
inline bool open_target_source(const std::string& path,
                               std::uint64_t expected_device_number,
                               OpenedTarget& target,
                               std::string& error,
                               ValidateFunction&& validate_device) {
    target = OpenedTarget{};
    if (expected_device_number == 0) {
        error = "A nonzero expected block-device identity is required";
        return false;
    }
    if (!same_target_path_identity(path, expected_device_number, error)) {
        return false;
    }
    if (!validate_device(static_cast<dev_t>(expected_device_number), error)) {
        error = "Target failed preliminary safety inspection: " + error;
        return false;
    }

    int raw_fd = -1;
    if (!open_target_read_only(
            path, raw_fd, error,
            [](const char* filename, int flags) {
                return ::open(filename, flags);
            })) {
        return false;
    }
    UniqueFd descriptor(raw_fd);

    const auto fstat_function = [](int fd, struct stat* metadata) {
        return ::fstat(fd, metadata);
    };
    const auto ioctl_function = [](int fd, unsigned long request, void* value) {
        return ::ioctl(fd, request, value);
    };

    DeviceSnapshot admitted{};
    if (!read_device_snapshot(descriptor.get(), admitted, error,
                              fstat_function, ioctl_function)) {
        return false;
    }
    if (admitted.device_number != expected_device_number) {
        error = "Opened target descriptor does not match the requested major/minor identity";
        return false;
    }
    if (admitted.read_only) {
        error = "Target device is read-only";
        return false;
    }
    if (!validate_device(static_cast<dev_t>(admitted.device_number), error)) {
        error = "Opened target failed descriptor-bound safety inspection: " + error;
        return false;
    }
    if (!same_target_path_identity(path, admitted.device_number, error)) {
        return false;
    }

    DeviceSnapshot current{};
    if (!read_device_snapshot(descriptor.get(), current, error,
                              fstat_function, ioctl_function)) {
        error = "Cannot revalidate opened target descriptor: " + error;
        return false;
    }
    if (!same_writable_device(admitted, current)) {
        error = "Target identity, capacity, or writable state changed during admission";
        return false;
    }
    if (!validate_device(static_cast<dev_t>(current.device_number), error)) {
        error = "Opened target failed final safety revalidation: " + error;
        return false;
    }

    target = OpenedTarget(std::move(descriptor), admitted);
    return target.valid();
}

inline bool open_target_source(const std::string& path,
                               std::uint64_t expected_device_number,
                               OpenedTarget& target,
                               std::string& error) {
    return open_target_source(
        path, expected_device_number, target, error,
        [](dev_t device_number, std::string& validation_error) {
            return validate_target_device(device_number, validation_error);
        });
}

}  // namespace iso_flash
