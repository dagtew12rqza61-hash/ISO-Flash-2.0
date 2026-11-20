#include "block_topology.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

int main() {
    if (!iso_flash::valid_sector_count("1") ||
        !iso_flash::valid_sector_count("4096") ||
        iso_flash::valid_sector_count("") ||
        iso_flash::valid_sector_count("0") ||
        iso_flash::valid_sector_count("12x") ||
        iso_flash::valid_sector_count("-1") ||
        iso_flash::valid_sector_count("18446744073709551616")) {
        std::cerr << "Strict sysfs sector-count parsing failed\n";
        return 1;
    }
    if (!iso_flash::path_is_inside("/sys/devices/sda/sda1", "/sys/devices/sda") ||
        iso_flash::path_is_inside("/sys/devices/sdaa", "/sys/devices/sda") ||
        iso_flash::path_is_inside("/sys/devices/sda-extra", "/sys/devices/sda")) {
        std::cerr << "Sysfs path-boundary validation failed\n";
        return 1;
    }

    const fs::path root = fs::temp_directory_path() /
        ("iso-flash-topology-" + std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    fs::remove_all(root, ec);
    ec.clear();

    const fs::path disk = root / "devices/usb/sda";
    const fs::path partition = disk / "sda1";
    const fs::path dm = root / "devices/virtual/dm-0";
    const fs::path unrelated = root / "devices/usb/sdb";
    fs::create_directories(partition / "slaves");
    fs::create_directories(disk / "slaves");
    fs::create_directories(dm / "slaves");
    fs::create_directories(unrelated / "slaves");

    const fs::path holders = disk / "holders";
    fs::create_directories(holders);
    bool holders_empty = false;
    if (!iso_flash::directory_is_empty_verified(holders, holders_empty) ||
        !holders_empty) {
        std::cerr << "Empty sysfs holders directory was not verified\n";
        fs::remove_all(root, ec);
        return 1;
    }
    {
        std::ofstream holder(holders / "dm-0");
        holder << "active";
    }
    if (!iso_flash::directory_is_empty_verified(holders, holders_empty) ||
        holders_empty) {
        std::cerr << "Non-empty sysfs holders directory was not detected\n";
        fs::remove_all(root, ec);
        return 1;
    }
    fs::remove(holders / "dm-0");
    fs::remove(holders);
    if (iso_flash::directory_is_empty_verified(holders, holders_empty)) {
        std::cerr << "Missing sysfs holders metadata was accepted\n";
        fs::remove_all(root, ec);
        return 1;
    }

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

    const fs::path missing_slaves = root / "devices/virtual/missing-slaves";
    fs::create_directories(missing_slaves);
    verified = true;
    visited.clear();
    if (iso_flash::depends_on(missing_slaves, canonical_disk, verified, visited) || verified) {
        std::cerr << "Missing sysfs slaves metadata was accepted\\n";
        fs::remove_all(root, ec);
        return 1;
    }

    fs::remove(dm / "slaves/sda1");
    fs::create_directory_symlink(root / "missing-device", dm / "slaves/broken");
    verified = true;
    visited.clear();
    if (iso_flash::depends_on(canonical_dm, canonical_disk, verified, visited) || verified) {
        std::cerr << "Broken sysfs dependency did not fail closed\n";
        fs::remove_all(root, ec);
        return 1;
    }

    fs::remove(dm / "slaves/broken");
    const fs::path cycle_a = root / "devices/virtual/cycle-a";
    const fs::path cycle_b = root / "devices/virtual/cycle-b";
    fs::create_directories(cycle_a / "slaves");
    fs::create_directories(cycle_b / "slaves");
    fs::create_directory_symlink(cycle_b, cycle_a / "slaves/cycle-b");
    fs::create_directory_symlink(cycle_a, cycle_b / "slaves/cycle-a");
    verified = true;
    visited.clear();
    if (iso_flash::depends_on(cycle_a, canonical_disk, verified, visited) || !verified) {
        std::cerr << "Cyclic sysfs dependency graph was not handled safely\n";
        fs::remove_all(root, ec);
        return 1;
    }

    fs::remove_all(root, ec);
    return 0;
}
