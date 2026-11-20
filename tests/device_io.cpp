#include "device_io.hpp"

#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

int main() {
    std::string error;
    int opened_fd = 123;
    int observed_flags = 0;
    std::string observed_path;
    const auto denied_open = [&](const char* path, int flags) {
        observed_path = path;
        observed_flags = flags;
        errno = EACCES;
        return -1;
    };

    if (iso_flash::open_target_read_only("/dev/test-device", opened_fd, error, denied_open) ||
        opened_fd != -1 || error.empty() || observed_path != "/dev/test-device" ||
        (observed_flags & O_ACCMODE) != O_RDONLY ||
        (observed_flags & O_CLOEXEC) == 0 ||
        (observed_flags & O_NONBLOCK) == 0) {
        std::cerr << "Read-only open failure was not handled correctly\n";
        return 1;
    }

    error.clear();
    const auto disconnected_open = [](const char*, int) {
        errno = ENODEV;
        return -1;
    };
    if (iso_flash::open_target_read_only("/dev/disconnected", opened_fd, error,
                                         disconnected_open) ||
        opened_fd != -1 || error.empty()) {
        std::cerr << "Disconnected target open was not rejected\n";
        return 1;
    }

    int descriptors[2] = {-1, -1};
    if (::pipe(descriptors) != 0) {
        std::cerr << "Cannot create descriptor test pipe\n";
        return 1;
    }
    const int owned_fd = descriptors[0];
    {
        iso_flash::UniqueFd descriptor(owned_fd);
        if (descriptor.get() != owned_fd) {
            ::close(descriptors[1]);
            return 1;
        }
        iso_flash::UniqueFd moved(std::move(descriptor));
        if (moved.get() != owned_fd || descriptor.get() != -1) {
            ::close(descriptors[1]);
            return 1;
        }
    }

    errno = 0;
    if (::fcntl(owned_fd, F_GETFD) != -1 || errno != EBADF) {
        ::close(owned_fd);
        ::close(descriptors[1]);
        std::cerr << "Owned descriptor was not closed by RAII\n";
        return 1;
    }
    ::close(descriptors[1]);
    return 0;
}
