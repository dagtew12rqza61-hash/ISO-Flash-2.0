#include "image_hash.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

bool fail(const char* message) {
    std::cerr << message << '\n';
    return false;
}

bool make_file(char* path, const char* data, std::size_t size) {
    const int fd = ::mkstemp(path);
    if (fd < 0) return false;
    const ssize_t written = ::write(fd, data, size);
    const bool ok = written == static_cast<ssize_t>(size);
    ::close(fd);
    return ok;
}

bool snapshot_for(int fd, iso_flash::ImageSnapshot& snapshot) {
    std::string error;
    return iso_flash::read_image_snapshot(
        fd, snapshot, error,
        [](int descriptor, struct stat* metadata) {
            return ::fstat(descriptor, metadata);
        });
}

}  // namespace

int main() {
    char path[] = "/tmp/iso-flash-hash-XXXXXX";
    if (!make_file(path, "abc", 3)) return fail("Cannot create hash fixture") ? 0 : 1;
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        ::unlink(path);
        return fail("Cannot open hash fixture") ? 0 : 1;
    }

    iso_flash::ImageSnapshot snapshot;
    if (!snapshot_for(fd, snapshot)) {
        ::close(fd);
        ::unlink(path);
        return fail("Cannot snapshot hash fixture") ? 0 : 1;
    }

    const auto complete = iso_flash::hash_image_sha256(fd, snapshot, [] { return false; });
    if (complete.status != iso_flash::ImageHashStatus::Completed ||
        complete.bytes_hashed != 3 ||
        complete.sha256 != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
        ::close(fd);
        ::unlink(path);
        return fail("Known SHA-256 fixture failed") ? 0 : 1;
    }

    int calls = 0;
    const auto interrupted_short_read = iso_flash::hash_image_sha256(
        fd, snapshot, [] { return false; },
        [&](int descriptor, void* buffer, std::size_t size, off_t offset) -> ssize_t {
            ++calls;
            if (calls == 1) {
                errno = EINTR;
                return -1;
            }
            return ::pread(descriptor, buffer, size > 1 ? 1 : size, offset);
        });
    if (interrupted_short_read.status != iso_flash::ImageHashStatus::Completed ||
        interrupted_short_read.sha256 != complete.sha256 || calls < 4) {
        ::close(fd);
        ::unlink(path);
        return fail("EINTR and short-read handling failed") ? 0 : 1;
    }

    const auto cancelled = iso_flash::hash_image_sha256(fd, snapshot, [] { return true; });
    if (cancelled.status != iso_flash::ImageHashStatus::Cancelled ||
        cancelled.bytes_hashed != 0) {
        ::close(fd);
        ::unlink(path);
        return fail("Hash cancellation was not honored") ? 0 : 1;
    }

    const auto read_error = iso_flash::hash_image_sha256(
        fd, snapshot, [] { return false; },
        [](int, void*, std::size_t, off_t) -> ssize_t {
            errno = EIO;
            return -1;
        });
    if (read_error.status != iso_flash::ImageHashStatus::Failed ||
        read_error.error_number != EIO) {
        ::close(fd);
        ::unlink(path);
        return fail("Read error was not preserved") ? 0 : 1;
    }

    const auto early_eof = iso_flash::hash_image_sha256(
        fd, snapshot, [] { return false; },
        [](int, void*, std::size_t, off_t) -> ssize_t { return 0; });
    if (early_eof.status != iso_flash::ImageHashStatus::Failed ||
        early_eof.error_number != EIO) {
        ::close(fd);
        ::unlink(path);
        return fail("Premature EOF was not rejected") ? 0 : 1;
    }

    int mutated = 0;
    const auto changed = iso_flash::hash_image_sha256(
        fd, snapshot, [] { return false; },
        [&](int descriptor, void* buffer, std::size_t size, off_t offset) -> ssize_t {
            const ssize_t count = ::pread(descriptor, buffer, size, offset);
            if (count > 0 && mutated++ == 0) {
                const int append_fd = ::open(path, O_WRONLY | O_APPEND | O_CLOEXEC);
                if (append_fd >= 0) {
                    (void)::write(append_fd, "x", 1);
                    ::close(append_fd);
                }
            }
            return count;
        });
    if (changed.status != iso_flash::ImageHashStatus::SourceChanged) {
        ::close(fd);
        ::unlink(path);
        return fail("Mutation during hashing was not detected") ? 0 : 1;
    }

    ::close(fd);
    ::unlink(path);
    return 0;
}
