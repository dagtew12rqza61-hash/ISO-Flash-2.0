#include "image_fd_validation.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

int main() {
    char path[] = "/tmp/iso-flash-fd-XXXXXX";
    const int writable_fd = ::mkstemp(path);
    if (writable_fd < 0) return 1;
    if (::write(writable_fd, "abc", 3) != 3) {
        ::close(writable_fd);
        ::unlink(path);
        return 1;
    }
    ::close(writable_fd);

    const int read_only_fd = ::open(path, O_RDONLY | O_CLOEXEC);
    const int read_write_fd = ::open(path, O_RDWR | O_CLOEXEC);
    ::unlink(path);
    if (read_only_fd < 0 || read_write_fd < 0) {
        if (read_only_fd >= 0) ::close(read_only_fd);
        if (read_write_fd >= 0) ::close(read_write_fd);
        return 1;
    }

    const auto accepted = iso_flash::validate_image_fd(read_only_fd);
    const auto rejected = iso_flash::validate_image_fd(read_write_fd);
    ::close(read_only_fd);
    ::close(read_write_fd);

    char large_path[] = "/tmp/iso-flash-cancel-XXXXXX";
    const int large_fd = ::mkstemp(large_path);
    if (large_fd < 0) return 1;
    ::unlink(large_path);
    constexpr off_t large_size = 2 * 1024 * 1024;
    if (::ftruncate(large_fd, large_size) != 0) {
        ::close(large_fd);
        return 1;
    }
    unsigned int cancellation_checks = 0;
    const auto cancelled = iso_flash::validate_image_fd(
        large_fd, [&cancellation_checks] {
            return ++cancellation_checks >= 2;
        });
    ::close(large_fd);

    if (!accepted.valid || accepted.bytes_hashed != 3 ||
        accepted.sha256 != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
        std::cerr << "Read-only image descriptor validation failed\n";
        return 1;
    }
    if (rejected.valid || rejected.error_number == 0) {
        std::cerr << "Writable image descriptor was not rejected\n";
        return 1;
    }
    if (cancelled.valid || cancelled.error_number != ECANCELED ||
        cancelled.bytes_hashed == 0 ||
        cancelled.bytes_hashed >= static_cast<std::uint64_t>(large_size)) {
        std::cerr << "Image validation did not stop at a cancellation boundary\\n";
        return 1;
    }
    return 0;
}
