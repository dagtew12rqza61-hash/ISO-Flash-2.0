#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace iso_flash {

inline bool decode_mountinfo_field(const std::string& input, std::string& output) {
    output.clear();
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '\\') {
            output.push_back(input[i]);
            continue;
        }
        if (i + 3 >= input.size() || input[i + 1] < '0' || input[i + 1] > '7' ||
            input[i + 2] < '0' || input[i + 2] > '7' ||
            input[i + 3] < '0' || input[i + 3] > '7') return false;
        const unsigned value = static_cast<unsigned>(input[i + 1] - '0') * 64U +
                               static_cast<unsigned>(input[i + 2] - '0') * 8U +
                               static_cast<unsigned>(input[i + 3] - '0');
        if (value != 040U && value != 011U && value != 012U && value != 0134U)
            return false;
        output.push_back(static_cast<char>(value));
        i += 3;
    }
    return true;
}

inline bool parse_mountinfo_device(const std::string& line, std::string& device) {
    std::istringstream stream(line);
    std::vector<std::string> fields;
    std::string field;
    while (stream >> field) fields.push_back(field);
    std::size_t separator = fields.size();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (fields[i] == "-") {
            separator = i;
            break;
        }
    }
    if (separator < 6 || fields.size() < separator + 4) return false;

    std::string decoded_root;
    std::string decoded_mountpoint;
    if (!decode_mountinfo_field(fields[3], decoded_root) ||
        !decode_mountinfo_field(fields[4], decoded_mountpoint)) return false;

    const std::string& id = fields[0];
    const std::string& parent = fields[1];
    if (id.empty() || parent.empty()) return false;
    for (const char c : id) if (c < '0' || c > '9') return false;
    for (const char c : parent) if (c < '0' || c > '9') return false;

    const std::string& dev = fields[2];
    const auto colon = dev.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= dev.size() ||
        dev.find(':', colon + 1) != std::string::npos) return false;
    for (std::size_t i = 0; i < dev.size(); ++i) {
        if (i == colon) continue;
        if (dev[i] < '0' || dev[i] > '9') return false;
    }
    device = dev;
    return true;
}

inline bool parse_mountinfo(std::istream& input, std::vector<std::string>& devices) {
    devices.clear();
    std::vector<std::string> parsed;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty()) return false;
        std::string device;
        if (!parse_mountinfo_device(line, device)) return false;
        parsed.push_back(device);
    }
    if (input.bad() || parsed.empty()) return false;
    devices = std::move(parsed);
    return true;
}

}  // namespace iso_flash
