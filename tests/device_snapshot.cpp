#include "device_snapshot.hpp"

#include <iostream>

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
        iso_flash::same_writable_device(zero_capacity, zero_capacity)) {
        std::cerr << "Device snapshot identity validation failed\n";
        return 1;
    }
    return 0;
}
