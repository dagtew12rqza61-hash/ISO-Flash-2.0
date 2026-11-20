#include "block_topology.hpp"

#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

int main() {
    const fs::path root = fs::temp_directory_path() /
        ("iso-flash-topology-" + std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    fs::remove_all(root, ec);
    ec.clear();

    const fs::path disk = root / "devices/usb/sda";
    const fs::path partition = disk / "sda1";
    const fs::path dm = root / "devices/virtual/dm-0";
    const fs::path unrelated = root / "devices/usb/sdb";
    fs::create_directories(partition);
    fs::create_directories(dm / "slaves");
    fs::create_directories(unrelated);

    fs::create_directory_symlink(partition, dm / "slaves/sda1");

    const fs::path canonical_disk = fs::canonical(disk);
    const fs::path canonical_dm = fs::canonical(dm);
    bool verified = true;
    std::set<std::string> visited;
    if (!iso_flash::depends_on(canonical_dm, canonical_disk, verified, visited) || !verified) {
        std::cerr << "Failed to detect a mounted device backed by the target disk\n";
        fs::remove_all(root, ec);
        return 1;
    }

    verified = true;
    visited.clear();
    if (iso_flash::depends_on(unrelated, canonical_disk, verified, visited) || !verified) {
        std::cerr << "Unrelated leaf device was incorrectly classified\n";
        fs::remove_all(root, ec);
        return 1;
    }

    fs::create_directory_symlink(root / "missing-device", dm / "slaves/broken");
    verified = true;
    visited.clear();
    if (iso_flash::depends_on(canonical_dm, canonical_disk, verified, visited) || verified) {
        std::cerr << "Broken sysfs dependency did not fail closed\n";
        fs::remove_all(root, ec);
        return 1;
    }

    fs::remove_all(root, ec);
    return 0;
}
