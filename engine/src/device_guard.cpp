#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace fs = std::filesystem;
static bool inside(const fs::path& child, const fs::path& parent) {
    const auto c = child.lexically_normal().string(), p = parent.lexically_normal().string();
    return c == p || (c.size() > p.size() && c.compare(0, p.size(), p) == 0 && c[p.size()] == '/');
}
static std::string first_line(const fs::path& p) { std::ifstream f(p); std::string s; std::getline(f, s); return s; }
bool validate_target(const std::string& device, std::string& error) {
    struct stat st{};
    if (::stat(device.c_str(), &st) != 0 || !S_ISBLK(st.st_mode)) { error = "Target is not a block device"; return false; }
    std::error_code ec;
    const fs::path real = fs::canonical(fs::path("/sys/dev/block") / (std::to_string(major(st.st_rdev)) + ":" + std::to_string(minor(st.st_rdev))), ec);
    if (ec || !fs::exists(real / "size")) { error = "Cannot resolve target in sysfs"; return false; }
    if (fs::exists(real / "partition")) { error = "Select the whole drive, not a partition"; return false; }
    const bool usb = real.string().find("/usb") != std::string::npos;
    if (first_line(real / "removable") != "1" && !usb) { error = "Device is not positively identified as removable USB storage"; return false; }
    struct stat root{};
    if (::stat("/", &root) == 0 && root.st_dev != 0) {
        const fs::path root_sys = fs::canonical(fs::path("/sys/dev/block") / (std::to_string(major(root.st_dev)) + ":" + std::to_string(minor(root.st_dev))), ec);
        if (!ec && inside(root_sys, real)) { error = "Refusing to overwrite the system disk"; return false; }
    }
    std::ifstream mounts("/proc/self/mountinfo"); std::string line;
    while (std::getline(mounts, line)) {
        const auto sep = line.find(" - "); if (sep == std::string::npos) continue;
        const auto first = line.substr(0, sep);
        const auto a = first.find(' '), b = first.find(' ', a + 1), c = first.find(' ', b + 1);
        if (c == std::string::npos) continue;
        const std::string dev = first.substr(b + 1, c - b - 1);
        std::error_code e; const fs::path mounted = fs::canonical(fs::path("/sys/dev/block") / dev, e);
        if (!e && inside(mounted, real)) { error = "Target contains a mounted filesystem"; return false; }
    }
    std::ifstream swaps("/proc/swaps"); std::getline(swaps, line);
    while (std::getline(swaps, line)) {
        const auto end = line.find(' '); const std::string name = line.substr(0, end); struct stat sw{};
        if (::stat(name.c_str(), &sw) == 0 && S_ISBLK(sw.st_mode)) {
            std::error_code e; const fs::path p = fs::canonical(fs::path("/sys/dev/block") / (std::to_string(major(sw.st_rdev)) + ":" + std::to_string(minor(sw.st_rdev))), e);
            if (!e && inside(p, real)) { error = "Target is used for swap"; return false; }
        }
    }
    return true;
}
