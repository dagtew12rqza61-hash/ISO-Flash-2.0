#include "mountinfo.hpp"

#include <cassert>
#include <sstream>
#include <string>
#include <vector>

int main() {
    {
        std::istringstream input(
            "36 25 8:1 / / rw,relatime - ext4 /dev/sda1 rw\n"
            "37 36 8:2 / /media/USB\\040DISK rw,nosuid - vfat /dev/sdb1 rw\n");
        std::vector<std::string> devices;
        assert(iso_flash::parse_mountinfo(input, devices));
        assert(devices.size() == 2);
        assert(devices[0] == "8:1");
        assert(devices[1] == "8:2");
    }
    {
        std::istringstream input("36 25 broken / / rw - ext4 /dev/sda1 rw\n");
        std::vector<std::string> devices;
        assert(!iso_flash::parse_mountinfo(input, devices));
        assert(devices.empty());
    }
    {
        std::istringstream input(
            "36 25 8:1 / / rw - ext4 /dev/sda1 rw\n"
            "bad trailing record\n");
        std::vector<std::string> devices;
        assert(!iso_flash::parse_mountinfo(input, devices));
        assert(devices.empty());
    }
    {
        std::istringstream input("36 25 8:1 / /bad\\999 rw - ext4 /dev/sda1 rw\n");
        std::vector<std::string> devices;
        assert(!iso_flash::parse_mountinfo(input, devices));
    }
    {
        std::istringstream input("");
        std::vector<std::string> devices;
        assert(!iso_flash::parse_mountinfo(input, devices));
    }
    return 0;
}
