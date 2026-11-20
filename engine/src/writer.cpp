#include <iostream>
#include <string>

int main(int argc, char** argv) {
    (void)argv;
    if (argc != 4) {
        std::cerr << "Usage: iso-flash-write --write IMAGE DEVICE\n";
        return 2;
    }
    std::cerr
        << "WRITE_DISABLED: destructive writing is disabled in this development build. "
        << "The device guard and privileged authorization path require further hardening "
        << "and independent tests before physical media can be modified.\n";
    return 3;
}
