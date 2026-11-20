#pragma once

#include "device_guard.hpp"
#include "device_io.hpp"
#include "device_snapshot.hpp"

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <utility>

namespace iso_flash {

namespace fs = std::filesystem;

struct WritableTarget {
    UniqueFd descriptor;
    DeviceSnapshot snapshot;
    fs::path sysfs_identity;

    WritableTarget() = default;
    WritableTarget(UniqueFd fd, DeviceSnapshot state, fs::path identity)
        : descriptor(std::move(fd)), snapshot(state),
          sysfs_identity(std::move(identity)) {}
    WritableTarget(const WritableTarget&) = delete;
    WritableTarget& operator=(const WritableTarget&) = delete;
    WritableTarget(WritableTarget&&) noexcept = default;
    WritableTarget& operator=(WritableTarget&&) noexcept = default;

    bool valid() const noexcept {
        return descriptor.get() >= 0 && snapshot.device_number != 0 &&
               snapshot.capacity_bytes > 0 && !snapshot.read_only &&
               !sysfs_identity.empty();
    }
};

inline bool valid_kernel_device_node_basename(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return false;
    for (const unsigned char ch : name) {
        const bool allowed =
            (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
        if (!allowed) return false;
    }
    return true;
}

inline bool resolve_kernel_device_node(dev_t expected_device,
                                       std::string& node_path,
                                       fs::path& sysfs_identity,
                                       std::string& error) {
    node_path.clear();
    sysfs_identity.clear();
    if (expected_device == 0) {
        error = "A nonzero block-device identity is required";
        return false;
    }

    const fs::path link = fs::path("/sys/dev/block") /
        (std::to_string(major(expected_device)) + ":" +
         std::to_string(minor(expected_device)));
    std::error_code ec;
    const fs::path resolved = fs::canonical(link, ec);
    if (ec || resolved.empty()) {
        error = "Cannot resolve requested block device in sysfs";
        return false;
    }

    std::ifstream dev_file(resolved / "dev");
    std::string dev_value;
    if (!dev_file || !std::getline(dev_file, dev_value) ||
        dev_value != std::to_string(major(expected_device)) + ":" +
                     std::to_string(minor(expected_device))) {
        error = "Sysfs device identity does not match the requested major/minor";
        return false;
    }

    std::ifstream uevent(resolved / "uevent");
    if (!uevent) {
        error = "Cannot read kernel block-device uevent metadata";
        return false;
    }

    std::string line;
    std::string device_name;
    while (std::getline(uevent, line)) {
        if (line.rfind("DEVNAME=", 0) == 0) {
            if (!device_name.empty()) {
                error = "Duplicate DEVNAME entries in kernel metadata";
                return false;
            }
            device_name = line.substr(8);
        }
    }
    if (uevent.bad() || !valid_kernel_device_node_basename(device_name)) {
        error = "Kernel supplied an invalid or unverifiable block-device node name";
        return false;
    }

    node_path = "/dev/" + device_name;
    sysfs_identity = resolved;
    return true;
}

inline bool inspect_block_node_path(const std::string& path,
                                    std::uint64_t expected_device_number,
                                    std::string& error) {
    struct stat metadata {};
    if (::lstat(path.c_str(), &metadata) != 0) {
        const int saved_error = errno;
        error = describe_device_error("Cannot inspect kernel device node", saved_error);
        return false;
    }
    if (!S_ISBLK(metadata.st_mode)) {
        error = "Kernel device node is not a direct block-device node";
        return false;
    }
    if (static_cast<std::uint64_t>(metadata.st_rdev) != expected_device_number) {
        error = "Kernel device node major/minor does not match the requested identity";
        return false;
    }
    return true;
}

inline constexpr int writable_target_open_flags() noexcept {
    return O_RDWR | O_CLOEXEC | O_EXCL | O_NOFOLLOW | O_NONBLOCK;
}

template <typename ValidateFunction>
inline bool open_writable_target_source(dev_t expected_device,
                                        WritableTarget& target,
                                        std::string& error,
                                        ValidateFunction&& validate_device) {
    target = WritableTarget{};
    std::string node_path;
    fs::path sysfs_identity;
    if (!resolve_kernel_device_node(expected_device, node_path,
                                    sysfs_identity, error)) {
        return false;
    }

    const auto expected_number = static_cast<std::uint64_t>(expected_device);
    if (!validate_device(expected_device, error)) {
        error = "Target failed preliminary safety inspection: " + error;
        return false;
    }
    if (!inspect_block_node_path(node_path, expected_number, error)) {
        return false;
    }

    int raw_fd = -1;
    do {
        raw_fd = ::open(node_path.c_str(), writable_target_open_flags());
    } while (raw_fd < 0 && errno == EINTR);
    if (raw_fd < 0) {
        const int saved_error = errno;
        error = describe_device_error("Cannot open target exclusively for writing", saved_error);
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
    if (admitted.device_number != expected_number || admitted.read_only) {
        error = "Opened writable descriptor does not match the requested writable device";
        return false;
    }
    if (!inspect_block_node_path(node_path, expected_number, error)) {
        return false;
    }

    std::string current_node_path;
    fs::path current_sysfs_identity;
    if (!resolve_kernel_device_node(expected_device, current_node_path,
                                    current_sysfs_identity, error) ||
        current_node_path != node_path || current_sysfs_identity != sysfs_identity) {
        error = "Sysfs device identity changed during writable admission";
        return false;
    }
    if (!validate_device(expected_device, error)) {
        error = "Opened writable target failed descriptor-bound safety inspection: " + error;
        return false;
    }

    DeviceSnapshot current{};
    if (!read_device_snapshot(descriptor.get(), current, error,
                              fstat_function, ioctl_function) ||
        !same_writable_device(admitted, current)) {
        error = "Writable target identity, capacity, or state changed during admission";
        return false;
    }
    if (!validate_device(expected_device, error)) {
        error = "Writable target failed final safety revalidation: " + error;
        return false;
    }

    target = WritableTarget(std::move(descriptor), admitted, sysfs_identity);
    if (!target.valid()) {
        error = "Writable target admission returned an invalid descriptor";
        return false;
    }
    return true;
}

inline bool open_writable_target_source(dev_t expected_device,
                                        WritableTarget& target,
                                        std::string& error) {
    return open_writable_target_source(
        expected_device, target, error,
        [](dev_t device_number, std::string& validation_error) {
            return validate_target_device(device_number, validation_error);
        });
}

}  // namespace iso_flash
