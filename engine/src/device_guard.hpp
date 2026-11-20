#pragma once

#include <cstdint>
#include <string>

bool validate_target(const std::string& device, std::string& error);
bool inspect_target_read_only(const std::string& device, std::string& error,
                              std::uint64_t& capacity_bytes);
