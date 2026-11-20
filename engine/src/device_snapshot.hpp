#pragma once

#include <cstdint>

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

}  // namespace iso_flash
