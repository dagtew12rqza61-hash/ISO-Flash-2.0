#include "device_guard.hpp"
#include "target_source.hpp"

#include <cstdint>
#include <string>
#include <sys/stat.h>

bool inspect_target_read_only(const std::string& device, std::string& error,
                              std::uint64_t& capacity_bytes,
                              dev_t expected_device_number) {
    capacity_bytes = 0;

    struct stat path_metadata{};
    if (::stat(device.c_str(), &path_metadata) != 0 ||
        !S_ISBLK(path_metadata.st_mode)) {
        error = "Target path is not a block device";
        return false;
    }

    const dev_t admitted_device_number =
        expected_device_number != 0 ? expected_device_number : path_metadata.st_rdev;

    iso_flash::OpenedTarget target;
    if (!iso_flash::open_target_source(
            device,
            static_cast<std::uint64_t>(admitted_device_number),
            target,
            error)) {
        return false;
    }

    if (!target.valid()) {
        error = "Read-only target admission returned an invalid descriptor";
        return false;
    }

    capacity_bytes = target.snapshot.capacity_bytes;
    return true;
}
