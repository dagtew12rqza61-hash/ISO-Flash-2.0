#include "mountinfo.hpp"

#include <sstream>
#include <string>
#include <vector>

int main() {
    {
        std::istringstream input(
            "36 25 8:1 / / rw,relatime - ext4 /dev/sda1 rw\n"
            "37 36 8:2 / /media/USB\\040DISK rw,nosuid - vfat /dev/sdb1 rw\n");
        std::vector<std::string> devices;
        if (!iso_flash::parse_mountinfo(input, devices)) return 1;
        if (devices.size() != 2 || devices[0] != "8:1" || devices[1] != "8:2") return 2;
    }
    {
        std::istringstream input("36 25 broken / / rw - ext4 /dev/sda1 rw\n");
        std::vector<std::string> devices;
        if (iso_flash::parse_mountinfo(input, devices) || !devices.empty()) return 3;
    }
    {
        std::istringstream input(
            "36 25 8:1 / / rw - ext4 /dev/sda1 rw\n"
            "bad trailing record\n");
        std::vector<std::string> devices;
        if (iso_flash::parse_mountinfo(input, devices) || !devices.empty()) return 4;
    }
    {
        std::istringstream input("36 25 8:1 / /bad\\999 rw - ext4 /dev/sda1 rw\n");
        std::vector<std::string> devices;
        if (iso_flash::parse_mountinfo(input, devices)) return 5;
    }
    {
        std::istringstream input("");
        std::vector<std::string> devices;
        if (iso_flash::parse_mountinfo(input, devices)) return 6;
    }
    return 0;
}
