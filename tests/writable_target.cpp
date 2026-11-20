#include "writable_target.hpp"

#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <vector>

int main() {
    if (!iso_flash::valid_kernel_device_node_basename("sda") ||
        !iso_flash::valid_kernel_device_node_basename("nvme0n1") ||
        !iso_flash::valid_kernel_device_node_basename("mmcblk0") ||
        !iso_flash::valid_kernel_device_node_basename("dm-0")) {
        std::cerr << "Valid kernel block-device names were rejected\n";
        return 1;
    }

    for (const std::string& invalid : std::vector<std::string>{
             "", ".", "..", "../sda", "sda/1", "/dev/sda", "sda\n",
             "sda name", "sda;touch", std::string({'s', 'd', 'a', '\0', 'e', 'v', 'i', 'l'})}) {
        if (iso_flash::valid_kernel_device_node_basename(invalid)) {
            std::cerr << "Unsafe device-node name was accepted: " << invalid << '\n';
            return 1;
        }
    }

    const int flags = iso_flash::writable_target_open_flags();
    for (const int required : {O_RDWR, O_CLOEXEC, O_EXCL, O_NOFOLLOW, O_NONBLOCK}) {
        if ((flags & required) != required) {
            std::cerr << "Writable target open flags are incomplete\n";
            return 1;
        }
    }

    iso_flash::WritableTarget empty;
    if (empty.valid()) {
        std::cerr << "Default writable target must be invalid\n";
        return 1;
    }

    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() /
        ("iso-flash-writable-target-" + std::to_string(static_cast<long long>(::getpid())));
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path sys_root = root / "sys/dev/block";
    const fs::path device = root / "sys/devices/usb1/8:1";
    const fs::path dev_root = root / "dev";
    fs::create_directories(sys_root);
    fs::create_directories(device);
    fs::create_directories(dev_root);
    fs::create_directory_symlink(device, sys_root / "8:1");
    {
        std::ofstream dev_file(device / "dev");
        dev_file << "8:1\\n";
        std::ofstream uevent(device / "uevent");
        uevent << "MAJOR=8\\nMINOR=1\\nDEVNAME=sda\\n";
    }
    std::string node_path;
    fs::path sysfs_identity;
    std::string resolve_error;
    const dev_t expected = makedev(8, 1);
    if (!iso_flash::resolve_kernel_device_node_at(
            expected, sys_root, dev_root, node_path, sysfs_identity, resolve_error) ||
        node_path != (dev_root / "sda").string() ||
        sysfs_identity != fs::canonical(device)) {
        std::cerr << "Synthetic sysfs identity resolution failed: " << resolve_error << '\\n';
        fs::remove_all(root, ec);
        return 1;
    }

    {
        std::ofstream dev_file(device / "dev");
        dev_file << "8:2\\n";
    }
    if (iso_flash::resolve_kernel_device_node_at(
            expected, sys_root, dev_root, node_path, sysfs_identity, resolve_error)) {
        std::cerr << "Mismatched sysfs major/minor was accepted\\n";
        fs::remove_all(root, ec);
        return 1;
    }
    {
        std::ofstream dev_file(device / "dev");
        dev_file << "8:1\\n";
        std::ofstream uevent(device / "uevent");
        uevent << "DEVNAME=\\nDEVNAME=sdb\\n";
    }
    if (iso_flash::resolve_kernel_device_node_at(
            expected, sys_root, dev_root, node_path, sysfs_identity, resolve_error)) {
        std::cerr << "Duplicate sysfs DEVNAME was accepted\\n";
        fs::remove_all(root, ec);
        return 1;
    }

    const fs::path second_device = root / "sys/devices/usb2/8:1";
    fs::create_directories(second_device);
    {
        std::ofstream dev_file(second_device / "dev");
        dev_file << "8:1\\n";
        std::ofstream uevent(second_device / "uevent");
        uevent << "DEVNAME=sda\\n";
    }
    fs::remove(sys_root / "8:1");
    fs::create_directory_symlink(second_device, sys_root / "8:1");
    if (!iso_flash::resolve_kernel_device_node_at(
            expected, sys_root, dev_root, node_path, sysfs_identity, resolve_error) ||
        sysfs_identity != fs::canonical(second_device)) {
        std::cerr << "Changed sysfs backing identity was not observed\\n";
        fs::remove_all(root, ec);
        return 1;
    }

    const fs::path regular_node = dev_root / "regular";
    { std::ofstream file(regular_node); file << "not a block node"; }
    if (iso_flash::inspect_block_node_path(regular_node.string(), 0, resolve_error)) {
        std::cerr << "Regular file was accepted as a block node\\n";
        fs::remove_all(root, ec);
        return 1;
    }
    fs::create_symlink(regular_node, dev_root / "symlink");
    if (iso_flash::inspect_block_node_path((dev_root / "symlink").string(), 0, resolve_error)) {
        std::cerr << "Symlink device node was accepted\\n";
        fs::remove_all(root, ec);
        return 1;
    }

    int injected_lstat_errno = ENOENT;
    const auto failed_lstat = [&](const char*, struct stat*) {
        errno = injected_lstat_errno;
        return -1;
    };
    if (iso_flash::inspect_block_node_path_with(
            "/dev/missing", 0, resolve_error, failed_lstat) ||
        resolve_error.find("Cannot inspect kernel device node") == std::string::npos) {
        std::cerr << "Injected lstat failure was not rejected\\n";
        fs::remove_all(root, ec);
        return 1;
    }
    const auto wrong_identity_lstat = [](const char*, struct stat* metadata) {
        metadata->st_mode = S_IFBLK | 0600;
        metadata->st_rdev = makedev(8, 2);
        return 0;
    };
    if (iso_flash::inspect_block_node_path_with(
            "/dev/substituted", static_cast<std::uint64_t>(expected),
            resolve_error, wrong_identity_lstat)) {
        std::cerr << "Substituted block-device identity was accepted\\n";
        fs::remove_all(root, ec);
        return 1;
    }
    const auto matching_lstat = [expected](const char*, struct stat* metadata) {
        metadata->st_mode = S_IFBLK | 0600;
        metadata->st_rdev = expected;
        return 0;
    };
    if (!iso_flash::inspect_block_node_path_with(
            "/dev/synthetic", static_cast<std::uint64_t>(expected),
            resolve_error, matching_lstat)) {
        std::cerr << "Matching injected block-device identity was rejected\\n";
        fs::remove_all(root, ec);
        return 1;
    }
    fs::remove_all(root, ec);
    return 0;
}
