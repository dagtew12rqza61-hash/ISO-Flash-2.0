#include "device_guard.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--check") {
        std::string error;
        if (!validate_target(argv[2], error)) {
            std::cerr << "TARGET_REJECTED: " << error << '\n';
            return 4;
        }
        std::cout << "TARGET_ACCEPTED_FOR_FURTHER_REVIEW: physical writing remains disabled.\n";
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
