#include "swapinfo.hpp"

#include <sstream>
#include <string>
#include <vector>

int main() {
    {
        std::istringstream input(
            "Filename\t\tType\t\tSize\tUsed\tPriority\n"
            "/dev/sda2\tpartition\t1048572\t0\t-2\n"
            "/swapfile\tfile\t2097148\t128\t-3\n");
        std::vector<std::string> names;
        if (!iso_flash::parse_swapinfo(input, names) || names.size() != 2 ||
            names[0] != "/dev/sda2" || names[1] != "/swapfile") return 1;
    }
    {
        std::istringstream input(
            "Filename\t\tType\t\tSize\tUsed\tPriority\n"
            "/dev/sda2\tpartition\tinvalid\t0\t-2\n");
        std::vector<std::string> names;
        if (iso_flash::parse_swapinfo(input, names) || !names.empty()) return 2;
    }
    {
        std::istringstream input(
            "Filename\t\tType\t\tSize\tUsed\tPriority\n"
            "/dev/sda2 partition 1024 0 -2 unexpected\n");
        std::vector<std::string> names;
        if (iso_flash::parse_swapinfo(input, names) || !names.empty()) return 3;
    }
    {
        std::istringstream input("");
        std::vector<std::string> names;
        if (iso_flash::parse_swapinfo(input, names) || !names.empty()) return 4;
    }
    {
        std::istringstream input(
            "Filename\t\tType\t\tSize\tUsed\tPriority\n"
            "/dev/sda2 partition 1024 0 -2\n"
            "malformed\n");
        std::vector<std::string> names;
        if (iso_flash::parse_swapinfo(input, names) || !names.empty()) return 5;
    }
    {
        std::istringstream input(
            "Filename\\t\\t\\t\\tType\\t\\tSize\\t\\tUsed\\t\\tPriority\\n"
            "/dev/loop9\\tpartition\\t1024\\t0\\t-2\\n");
        std::vector<std::string> names;
        if (!iso_flash::parse_swapinfo(input, names) ||
            names.size() != 1 || names[0] != "/dev/loop9") return 6;
    }
    {
        std::istringstream input(
            "Filename Type Size Used Other\\n");
        std::vector<std::string> names;
        if (iso_flash::parse_swapinfo(input, names) || !names.empty()) return 7;
    }
    return 0;
}
