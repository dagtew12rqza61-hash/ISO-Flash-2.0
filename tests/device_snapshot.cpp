#include "device_snapshot.hpp"

#include <cerrno>
#include <iostream>
#include <sys/sysmacros.h>

int main() {
    const iso_flash::DeviceSnapshot initial{2049, 16U * 1024U * 1024U, false};
    const iso_flash::DeviceSnapshot unchanged{2049, 16U * 1024U * 1024U, false};
    const iso_flash::DeviceSnapshot replaced{2050, 16U * 1024U * 1024U, false};
    const iso_flash::DeviceSnapshot resized{2049, 32U * 1024U * 1024U, false};
    const iso_flash::DeviceSnapshot readonly{2049, 16U * 1024U * 1024U, true};
    const iso_flash::DeviceSnapshot zero_capacity{2049, 0, false};

    if (!iso_flash::same_writable_device(initial, unchanged) ||
        iso_flash::same_writable_device(initial, replaced) ||
        iso_flash::same_writable_device(initial, resized) ||
        iso_flash::same_writable_device(initial, readonly) ||
        iso_flash::same_writable_device(initial, zero_capacity) ||
        iso_flash::same_writable_device(zero_capacity, zero_capacity) ||
        !iso_flash::same_opened_target(initial, initial.device_number, unchanged) ||
        !iso_flash::matches_expected_device(initial, initial.device_number) ||
        iso_flash::matches_expected_device(initial, replaced.device_number) ||
        !iso_flash::matches_expected_device(initial, 0) ||
        iso_flash::same_opened_target(initial, replaced.device_number, unchanged) ||
        iso_flash::same_opened_target(initial, initial.device_number, replaced) ||
        iso_flash::same_opened_target(initial, initial.device_number, resized) ||
        iso_flash::same_opened_target(initial, initial.device_number, readonly)) {
        std::cerr << "Device snapshot identity validation failed\n";
        return 1;
    }

    bool fail_fstat = false;
    unsigned long fail_ioctl_request = 0;
    int injected_errno = EIO;
    std::uint64_t injected_capacity = 16U * 1024U * 1024U;
    int injected_read_only = 0;
    const auto fake_fstat = [&](int, struct stat* metadata) {
        if (fail_fstat) {
            errno = injected_errno;
            return -1;
        }
        metadata->st_mode = S_IFBLK | 0600;
        metadata->st_rdev = makedev(8, 1);
        return 0;
    };
    const auto fake_ioctl = [&](int, unsigned long request, void* value) {
        if (request == fail_ioctl_request) {
            errno = injected_errno;
            return -1;
        }
        if (request == BLKGETSIZE64) {
            *static_cast<std::uint64_t*>(value) = injected_capacity;
            return 0;
        }
        if (request == BLKROGET) {
            *static_cast<int*>(value) = injected_read_only;
            return 0;
        }
        errno = EINVAL;
        return -1;
    };

    iso_flash::DeviceSnapshot captured{};
    std::string error;
    if (!iso_flash::read_device_snapshot(7, captured, error, fake_fstat, fake_ioctl) ||
        captured.device_number != static_cast<std::uint64_t>(makedev(8, 1)) ||
        captured.capacity_bytes != injected_capacity || captured.read_only) {
        std::cerr << "Synthetic device snapshot read failed: " << error << '\n';
        return 1;
    }

    fail_fstat = true;
    injected_errno = ENODEV;
    error.clear();
    if (iso_flash::read_device_snapshot(7, captured, error, fake_fstat, fake_ioctl) ||
        error.find("disconnected or is no longer available") == std::string::npos ||
        captured.capacity_bytes != 0) {
        std::cerr << "Disconnected device metadata read was not diagnosed: " << error << '\n';
        return 1;
    }

    fail_fstat = false;
    fail_ioctl_request = BLKGETSIZE64;
    injected_errno = EIO;
    error.clear();
    if (iso_flash::read_device_snapshot(7, captured, error, fake_fstat, fake_ioctl) ||
        error.find("device I/O error") == std::string::npos) {
        std::cerr << "Capacity read error was not diagnosed: " << error << '\n';
        return 1;
    }

    fail_ioctl_request = BLKROGET;
    injected_errno = ENODEV;
    error.clear();
    if (iso_flash::read_device_snapshot(7, captured, error, fake_fstat, fake_ioctl) ||
        error.find("disconnected or is no longer available") == std::string::npos) {
        std::cerr << "Device disconnect during read-only query was not diagnosed: " << error << '\n';
        return 1;
    }

    fail_ioctl_request = 0;
    injected_capacity = 0;
    error.clear();
    if (iso_flash::read_device_snapshot(7, captured, error, fake_fstat, fake_ioctl) ||
        error.empty()) {
        std::cerr << "Zero-capacity device was not rejected\n";
        return 1;
    }

    injected_capacity = 16U * 1024U * 1024U;
    injected_read_only = 1;
    error.clear();
    if (!iso_flash::read_device_snapshot(7, captured, error, fake_fstat, fake_ioctl) ||
        !captured.read_only ||
        iso_flash::same_writable_device(captured, captured)) {
        std::cerr << "Read-only state was not preserved in the snapshot\n";
        return 1;
    }

    return 0;
}
