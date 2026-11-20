#include "device_guard.hpp"
#include "device_snapshot.hpp"
#include "device_io.hpp"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

bool inspect_target_read_only(const std::string& device, std::string& error,
                              std::uint64_t& capacity_bytes,
                              dev_t expected_device_number) {
    capacity_bytes = 0;
    struct stat path_before{};
    if (::stat(device.c_str(), &path_before) != 0 || !S_ISBLK(path_before.st_mode)) {
        error = "Target path is not a block device";
        return false;
    }
    const std::uint64_t path_device_number =
        static_cast<std::uint64_t>(path_before.st_rdev);
    if (expected_device_number != 0 &&
        path_device_number != static_cast<std::uint64_t>(expected_device_number)) {
        error = "Target path does not match the requested device identity";
        return false;
    }
    if (!validate_target_device(path_before.st_rdev, error)) {
        error = "Target failed preliminary safety inspection: " + error;
        return false;
    }

    int raw_fd = -1;
    const auto open_function = [](const char* path, int flags) {
        return ::open(path, flags);
    };
    if (!iso_flash::open_target_read_only(device, raw_fd, error, open_function)) {
        return false;
    }
    const iso_flash::UniqueFd fd(raw_fd);

    const auto fstat_function = [](int descriptor, struct stat* metadata) {
        return ::fstat(descriptor, metadata);
    };
    const auto ioctl_function = [](int descriptor, unsigned long request, void* value) {
        return ::ioctl(descriptor, request, value);
    };

    iso_flash::DeviceSnapshot original{};
    bool ok = iso_flash::read_device_snapshot(
        fd.get(), original, error, fstat_function, ioctl_function);
    if (ok && (!iso_flash::matches_expected_device(
                   original, path_device_number) ||
               !iso_flash::matches_expected_device(
                   original, static_cast<std::uint64_t>(expected_device_number)))) {
        error = "Opened device does not match the requested major/minor identity";
        ok = false;
    }
    if (ok && !validate_target_device(static_cast<dev_t>(original.device_number), error)) {
        error = "Opened device failed descriptor-bound safety inspection: " + error;
        ok = false;
    }
    if (ok && !iso_flash::same_writable_device(original, original)) {
        error = "Target has zero capacity or is read-only";
        ok = false;
    }

    struct stat path_after{};
    iso_flash::DeviceSnapshot opened_after{};
    if (ok && (::stat(device.c_str(), &path_after) != 0 ||
               !S_ISBLK(path_after.st_mode) ||
               static_cast<std::uint64_t>(path_after.st_rdev) != original.device_number)) {
        error = "Target path no longer identifies the opened block device";
        ok = false;
    }
    if (ok && !iso_flash::read_device_snapshot(
                  fd.get(), opened_after, error, fstat_function, ioctl_function)) {
        error = "Cannot revalidate target identity after opening: " + error;
        ok = false;
    }
    if (ok && !iso_flash::same_opened_target(
                  original, static_cast<std::uint64_t>(path_after.st_rdev), opened_after)) {
        error = "Target identity, capacity, or writable state changed during validation";
        ok = false;
    }
    if (ok && !validate_target_device(static_cast<dev_t>(original.device_number), error)) {
        error = "Opened device failed safety revalidation: " + error;
        ok = false;
    }

    if (!ok) {
        return false;
    }
    capacity_bytes = original.capacity_bytes;
    return true;
}
