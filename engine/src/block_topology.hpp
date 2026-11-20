#pragma once

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>

namespace iso_flash {
namespace fs = std::filesystem;

inline bool path_is_inside(const fs::path& child, const fs::path& parent) {
    const auto c = child.lexically_normal().string();
    const auto p = parent.lexically_normal().string();
    return c == p || (c.size() > p.size() && c.compare(0, p.size(), p) == 0 && c[p.size()] == '/');
}

inline bool directory_is_empty_verified(const fs::path& path, bool& empty) {
    std::error_code ec;
    const fs::file_status status = fs::status(path, ec);
    if (ec || !fs::exists(status) || !fs::is_directory(status)) {
        return false;
    }
    empty = fs::is_empty(path, ec);
    return !ec;
}

inline bool valid_sector_count(const std::string& value) {
    std::uint64_t sectors = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), sectors);
    return !value.empty() && result.ec == std::errc{} &&
           result.ptr == value.data() + value.size() && sectors > 0;
}

// Walk from a mounted/active device toward its backing devices through sysfs slaves.
// A missing slaves directory is valid for leaf devices; unreadable or broken links fail closed.
inline bool depends_on(const fs::path& current, const fs::path& target,
                       bool& verified, std::set<std::string>& visited) {
    std::error_code ec;
    const fs::path resolved = fs::canonical(current, ec);
    if (ec) {
        verified = false;
        return false;
    }
    if (resolved == target || path_is_inside(resolved, target)) {
        return true;
    }

    if (!visited.insert(resolved.string()).second) {
        return false;
    }

    const fs::path slaves = resolved / "slaves";
    const bool has_slaves = fs::exists(slaves, ec);
    if (ec) {
        verified = false;
        return false;
    }
    if (!has_slaves) {
        return false;
    }

    fs::directory_iterator it(slaves, ec);
    const fs::directory_iterator end;
    if (ec) {
        verified = false;
        return false;
    }
    while (it != end) {
        const fs::path slave = it->path();
        if (depends_on(slave, target, verified, visited)) {
            return true;
        }
        if (!verified) {
            return false;
        }
        it.increment(ec);
        if (ec) {
            verified = false;
            return false;
        }
    }
    return false;
}
}  // namespace iso_flash
