#include "device_guard.hpp"
#include "device_snapshot.hpp"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

bool inspect_target_read_only(const std::string& device, std::string& error,
                              std::uint64_t& capacity_bytes) {
    capacity_bytes = 0;
    if (!validate_target(device, error)) {
        return false;
    }

    struct stat path_before{};
    if (::stat(device.c_str(), &path_before) != 0 || !S_ISBLK(path_before.st_mode)) {
        error = "Target path changed or is no longer a block device";
        return false;
    }

    const int fd = ::open(device.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        error = "Cannot open target read-only";
        return false;
    }

    const auto fstat_function = [](int descriptor, struct stat* metadata) {
        return ::fstat(descriptor, metadata);
    };
    const auto ioctl_function = [](int descriptor, unsigned long request, void* value) {
        return ::ioctl(descriptor, request, value);
    };

    iso_flash::DeviceSnapshot original{};
    bool ok = iso_flash::read_device_snapshot(
        fd, original, error, fstat_function, ioctl_function);
    if (ok && original.device_number != static_cast<std::uint64_t>(path_before.st_rdev)) {
        error = "Opened device identity does not match the validated path";
        ok = false;
    }
    if (ok && !iso_flash::same_writable_device(original, original)) {
        error = "Target has zero capacity or is read-only";
        ok = false;
    }

    if (ok && !validate_target(device, error)) {
        error = "Target failed safety revalidation after open: " + error;
        ok = false;
    }

    struct stat path_after{};
    iso_flash::DeviceSnapshot opened_after{};
    if (ok && (::stat(device.c_str(), &path_after) != 0 ||
               !S_ISBLK(path_after.st_mode))) {
        error = "Cannot revalidate target path after opening";
        ok = false;
    }
    if (ok && !iso_flash::read_device_snapshot(
                  fd, opened_after, error, fstat_function, ioctl_function)) {
        error = "Cannot revalidate target identity after opening: " + error;
        ok = false;
    }
    if (ok) {
        const iso_flash::DeviceSnapshot path_snapshot{
            static_cast<std::uint64_t>(path_after.st_rdev),
            opened_after.capacity_bytes, opened_after.read_only};
        if (!iso_flash::same_writable_device(original, path_snapshot) ||
            !iso_flash::same_writable_device(original, opened_after)) {
            error = "Target identity, capacity, or writable state changed during validation";
            ok = false;
        }
    }

    ::close(fd);
    if (!ok) {
        return false;
    }
    capacity_bytes = original.capacity_bytes;
    return true;
}
