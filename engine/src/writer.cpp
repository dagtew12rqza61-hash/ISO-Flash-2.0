#include "device_guard.hpp"

#include <cstdint>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--check") {
        std::string error;
        std::uint64_t capacity_bytes = 0;
        if (!inspect_target_read_only(argv[2], error, capacity_bytes)) {
            std::cerr << "TARGET_REJECTED: " << error << '\n';
            return 4;
        }
        std::cout << "TARGET_ACCEPTED_FOR_FURTHER_REVIEW: "
                  << capacity_bytes << " bytes; physical writing remains disabled.\n";
        return 0;
    }
    if (argc != 4 || std::string(argv[1]) != "--write") {
        std::cerr << "Usage: iso-flash-write --check DEVICE\n"
                     "       iso-flash-write --write IMAGE DEVICE\n";
        return 2;
    }
    std::cerr
        << "WRITE_DISABLED: destructive writing is disabled in this development build. "
        << "Target validation alone is not sufficient authorization to modify physical media.\n";
    return 3;
}
