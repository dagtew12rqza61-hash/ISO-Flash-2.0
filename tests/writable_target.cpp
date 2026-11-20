#include "writable_target.hpp"

#include <fcntl.h>
#include <iostream>
#include <string>

int main() {
    if (!iso_flash::valid_kernel_device_node_basename("sda") ||
        !iso_flash::valid_kernel_device_node_basename("nvme0n1") ||
        !iso_flash::valid_kernel_device_node_basename("mmcblk0") ||
        !iso_flash::valid_kernel_device_node_basename("dm-0")) {
        std::cerr << "Valid kernel block-device names were rejected\n";
        return 1;
    }

    for (const std::string invalid : {
             "", ".", "..", "../sda", "sda/1", "/dev/sda", "sda\n",
             "sda name", "sda;touch", std::string("sda\\0evil", 8)}) {
        if (iso_flash::valid_kernel_device_node_basename(invalid)) {
            std::cerr << "Unsafe device-node name was accepted: " << invalid << '\n';
            return 1;
        }
    }

    const int flags = iso_flash::writable_target_open_flags();
    for (const int required : {O_RDWR, O_CLOEXEC, O_EXCL, O_NOFOLLOW, O_NONBLOCK}) {
        if ((flags & required) != required) {
            std::cerr << "Writable target open flags are incomplete\n";
            return 1;
        }
    }

    iso_flash::WritableTarget empty;
    if (empty.valid()) {
        std::cerr << "Default writable target must be invalid\n";
        return 1;
    }
    return 0;
}
