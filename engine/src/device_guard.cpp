#include "device_guard.hpp"
#include "mountinfo.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>
#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace fs = std::filesystem;

static bool inside(const fs::path& child, const fs::path& parent) {
    const auto c = child.lexically_normal().string();
    const auto p = parent.lexically_normal().string();
    return c == p || (c.size() > p.size() && c.compare(0, p.size(), p) == 0 && c[p.size()] == '/');
}
static std::string first_line(const fs::path& p) {
    std::ifstream f(p);
    std::string s;
    std::getline(f, s);
    return s;
}
static fs::path sys_device(dev_t number, std::error_code& ec) {
    return fs::canonical(fs::path("/sys/dev/block") /
        (std::to_string(major(number)) + ":" + std::to_string(minor(number))), ec);
}

static bool has_usb_transport_ancestor(const fs::path& device) {
    fs::path current = device;
    while (!current.empty() && current != current.root_path()) {
        std::error_code ec;
        const fs::path subsystem = fs::canonical(current / "subsystem", ec);
        if (!ec && subsystem == fs::path("/sys/bus/usb")) {
            return true;
        }
        current = current.parent_path();
    }
    return false;
}

static bool depends_on(const fs::path& current, const fs::path& target,
                       bool& verified, std::set<std::string>& visited) {
    std::error_code ec;
    const fs::path resolved = fs::canonical(current, ec);
    if (ec) {
        verified = false;
        return false;
    }
    if (resolved == target || inside(resolved, target)) {
        return true;
    }

    const std::string key = resolved.string();
    if (!visited.insert(key).second) {
        return false;
    }

    const fs::path slaves = resolved / "slaves";
    if (!fs::exists(slaves, ec)) {
        if (ec) verified = false;
        return false;
    }

    fs::directory_iterator it(slaves, ec);
    const fs::directory_iterator end;
    if (ec) {
        verified = false;
        return false;
    }
    for (; it != end; it.increment(ec)) {
        if (ec) {
            verified = false;
            return false;
        }
        if (depends_on(it->path(), target, verified, visited)) {
            return true;
        }
        if (!verified) {
            return false;
        }
    }
    if (ec) {
        verified = false;
    }
    return false;
}

bool validate_target(const std::string& device, std::string& error) {
    struct stat st{};
    if (::stat(device.c_str(), &st) != 0 || !S_ISBLK(st.st_mode)) {
        error = "Target is not a block device";
        return false;
    }
    std::error_code ec;
    const fs::path real = sys_device(st.st_rdev, ec);
    if (ec || !fs::exists(real / "size")) {
        error = "Cannot resolve target in sysfs";
        return false;
    }
    if (fs::exists(real / "partition")) {
        error = "Select the whole drive, not a partition";
        return false;
    }
    if (!has_usb_transport_ancestor(real)) {
        error = "Device has no verified USB transport ancestor in sysfs";
        return false;
    }
    const std::string read_only = first_line(real / "ro");
    if (read_only != "0") {
        error = "Cannot verify writable state or target is read-only";
        return false;
    }
    const std::string sectors = first_line(real / "size");
    try {
        if (sectors.empty() || std::stoull(sectors) == 0) {
            error = "Target reports zero capacity";
            return false;
        }
    } catch (...) {
        error = "Cannot verify target capacity";
        return false;
    }
    if (fs::exists(real / "holders")) {
        ec.clear();
        const bool empty = fs::is_empty(real / "holders", ec);
        if (ec || !empty) {
            error = "Cannot verify holders or target backs another block device";
            return false;
        }
    }

    struct stat root{};
    if (::stat("/", &root) != 0) {
        error = "Cannot identify the system root device";
        return false;
    }
    const fs::path root_sys = sys_device(root.st_dev, ec);
    if (ec) {
        error = "Cannot resolve the system root device in sysfs";
        return false;
    }
    bool root_topology_verified = true;
    std::set<std::string> visited_root_devices;
    if (depends_on(root_sys, real, root_topology_verified, visited_root_devices)) {
        error = "Refusing to overwrite the system disk or one of its backing devices";
        return false;
    }
    if (!root_topology_verified) {
        error = "Cannot verify the complete system disk dependency chain";
        return false;
    }

    std::ifstream mounts("/proc/self/mountinfo");
    if (!mounts) {
        error = "Cannot inspect mounted filesystems";
        return false;
    }
    std::vector<std::string> mounted_devices;
    if (!iso_flash::parse_mountinfo(mounts, mounted_devices)) {
        error = "Cannot parse complete mounted filesystem metadata";
        return false;
    }
    for (const auto& dev : mounted_devices) {
        if (dev.rfind("0:", 0) == 0) continue;
        std::error_code mount_ec;
        const fs::path mounted = fs::canonical(fs::path("/sys/dev/block") / dev, mount_ec);
        if (mount_ec) {
            error = "Cannot resolve a mounted block device";
            return false;
        }
        bool topology_verified = true;
        std::set<std::string> visited;
        if (depends_on(mounted, real, topology_verified, visited)) {
            error = "Target or one of its backing devices contains a mounted filesystem";
            return false;
        }
        if (!topology_verified) {
            error = "Cannot verify mounted block-device dependencies";
            return false;
        }
    }

    std::ifstream swaps("/proc/swaps");
    if (!swaps) {
        error = "Cannot inspect active swap devices";
        return false;
    }
    std::string line;
    std::getline(swaps, line);
    while (std::getline(swaps, line)) {
        const auto end = line.find(' ');
        const std::string name = line.substr(0, end);
        struct stat sw{};
        if (::stat(name.c_str(), &sw) != 0) {
            error = "Cannot verify an active swap entry";
            return false;
        }
        if (S_ISBLK(sw.st_mode)) {
            std::error_code swap_ec;
            const fs::path swap_dev = sys_device(sw.st_rdev, swap_ec);
            if (swap_ec) {
                error = "Cannot resolve an active swap device";
                return false;
            }
            bool topology_verified = true;
            std::set<std::string> visited;
            if (depends_on(swap_dev, real, topology_verified, visited)) {
                error = "Target or one of its backing devices is used for swap";
                return false;
            }
            if (!topology_verified) {
                error = "Cannot verify active swap device dependencies";
                return false;
            }
        }
    }
    if (swaps.bad()) {
        error = "Failed while reading active swap metadata";
        return false;
    }
    return true;
}
