#pragma once

#include <sstream>
#include <string>
#include <vector>

namespace iso_flash {

inline bool parse_swapinfo(std::istream& input, std::vector<std::string>& names) {
    names.clear();
    std::string line;
    if (!std::getline(input, line) ||
        line != "Filename\t\tType\t\tSize\tUsed\tPriority") {
        return false;
    }

    std::vector<std::string> parsed;
    while (std::getline(input, line)) {
        if (line.empty()) return false;
        std::istringstream fields(line);
        std::string name;
        std::string type;
        std::string size;
        std::string used;
        std::string priority;
        std::string extra;
        if (!(fields >> name >> type >> size >> used >> priority) ||
            (fields >> extra) || name.empty() || type.empty() ||
            size.empty() || used.empty() || priority.empty()) {
            return false;
        }
        for (const char c : size) if (c < '0' || c > '9') return false;
        for (const char c : used) if (c < '0' || c > '9') return false;
        std::size_t priority_index = priority[0] == '-' ? 1U : 0U;
        if (priority_index == priority.size()) return false;
        for (; priority_index < priority.size(); ++priority_index) {
            if (priority[priority_index] < '0' ||
                priority[priority_index] > '9') return false;
        }
        parsed.push_back(std::move(name));
    }
    if (input.bad()) return false;
    names = std::move(parsed);
    return true;
}

}  // namespace iso_flash
