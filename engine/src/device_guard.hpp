#pragma once

#include <cstdint>
#include <string>
#include <sys/types.h>

bool validate_target(const std::string& device, std::string& error);
bool validate_target_device(dev_t device_number, std::string& error);
bool inspect_target_read_only(const std::string& device, std::string& error,
                              std::uint64_t& capacity_bytes);
