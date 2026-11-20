#include "device_guard.hpp"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <linux/fs.h>
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

    struct stat opened{};
    std::uint64_t capacity_before = 0;
    int read_only_before = 1;
    bool ok = true;

    if (::fstat(fd, &opened) != 0 || !S_ISBLK(opened.st_mode) ||
        opened.st_rdev != path_before.st_rdev) {
        error = "Opened device identity does not match the validated path";
        ok = false;
    }
    if (ok && (::ioctl(fd, BLKGETSIZE64, &capacity_before) != 0 ||
               ::ioctl(fd, BLKROGET, &read_only_before) != 0)) {
        error = "Cannot read block device capacity or read-only state";
        ok = false;
    }
    if (ok && (capacity_before == 0 || read_only_before != 0)) {
        error = "Target has zero capacity or is read-only";
        ok = false;
    }

    if (ok && !validate_target(device, error)) {
        error = "Target failed safety revalidation after open: " + error;
        ok = false;
    }

    struct stat path_after{};
    struct stat opened_after{};
    std::uint64_t capacity_after = 0;
    int read_only_after = 1;
    if (ok && (::stat(device.c_str(), &path_after) != 0 ||
               ::fstat(fd, &opened_after) != 0 ||
               ::ioctl(fd, BLKGETSIZE64, &capacity_after) != 0 ||
               ::ioctl(fd, BLKROGET, &read_only_after) != 0)) {
        error = "Cannot revalidate target identity after opening";
        ok = false;
    }
    if (ok && (path_after.st_rdev != opened.st_rdev ||
               opened_after.st_rdev != opened.st_rdev ||
               capacity_after != capacity_before || read_only_after != 0)) {
        error = "Target identity or capacity changed during validation";
        ok = false;
    }

    ::close(fd);
    if (!ok) {
        return false;
    }
    capacity_bytes = capacity_before;
    return true;
}
