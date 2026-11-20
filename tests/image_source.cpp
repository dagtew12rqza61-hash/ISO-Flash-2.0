#include "image_source.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

bool create_file(char* path, const char* data, std::size_t size) {
    const int fd = ::mkstemp(path);
    if (fd < 0) return false;
    const ssize_t written = ::write(fd, data, size);
    const bool ok = written == static_cast<ssize_t>(size);
    ::close(fd);
    return ok;
}

}  // namespace

int main() {
    char image_path[] = "/tmp/iso-flash-image-XXXXXX";
    if (!create_file(image_path, "valid-image", 11)) {
        std::cerr << "Cannot create image fixture\n";
        return 1;
    }

    int image_fd = -1;
    iso_flash::ImageSnapshot original;
    std::string error;
    int attempts = 0;
    const auto retrying_fstat = [&](int fd, struct stat* metadata) {
        ++attempts;
        if (attempts == 1) {
            errno = EINTR;
            return -1;
        }
        return ::fstat(fd, metadata);
    };
    if (!iso_flash::open_image_read_only(
            image_path, image_fd, original, error,
            [](const char* path, int flags) { return ::open(path, flags); },
            retrying_fstat) ||
        image_fd < 0 || original.size_bytes != 11 || attempts != 2) {
        std::cerr << "Valid ISO descriptor was rejected: " << error << '\n';
        ::unlink(image_path);
        return 1;
    }

    iso_flash::ImageSnapshot unchanged = original;
    if (!iso_flash::same_image_snapshot(original, unchanged)) {
        std::cerr << "Unchanged ISO snapshot did not match\n";
        ::close(image_fd);
        ::unlink(image_path);
        return 1;
    }
    ++unchanged.size_bytes;
    if (iso_flash::same_image_snapshot(original, unchanged)) {
        std::cerr << "Changed ISO size was not detected\n";
        ::close(image_fd);
        ::unlink(image_path);
        return 1;
    }

    const int append_fd = ::open(image_path, O_WRONLY | O_APPEND | O_CLOEXEC);
    if (append_fd < 0 || ::write(append_fd, "x", 1) != 1) {
        std::cerr << "Cannot mutate image fixture\n";
        if (append_fd >= 0) ::close(append_fd);
        ::close(image_fd);
        ::unlink(image_path);
        return 1;
    }
    ::close(append_fd);
    iso_flash::ImageSnapshot changed;
    const auto real_fstat = [](int fd, struct stat* metadata) {
        return ::fstat(fd, metadata);
    };
    if (!iso_flash::read_image_snapshot(image_fd, changed, error, real_fstat) ||
        iso_flash::same_image_snapshot(original, changed)) {
        std::cerr << "Source mutation was not detected\n";
        ::close(image_fd);
        ::unlink(image_path);
        return 1;
    }
    ::close(image_fd);

    char empty_path[] = "/tmp/iso-flash-empty-XXXXXX";
    const int empty_fd = ::mkstemp(empty_path);
    if (empty_fd < 0) {
        ::unlink(image_path);
        return 1;
    }
    iso_flash::ImageSnapshot empty_snapshot;
    if (iso_flash::read_image_snapshot(empty_fd, empty_snapshot, error, real_fstat) ||
        error.find("empty") == std::string::npos) {
        std::cerr << "Empty ISO source was accepted\n";
        ::close(empty_fd);
        ::unlink(empty_path);
        ::unlink(image_path);
        return 1;
    }
    ::close(empty_fd);
    ::unlink(empty_path);

    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) {
        ::unlink(image_path);
        return 1;
    }
    iso_flash::ImageSnapshot pipe_snapshot;
    if (iso_flash::read_image_snapshot(pipe_fds[0], pipe_snapshot, error, real_fstat) ||
        error.find("regular file") == std::string::npos) {
        std::cerr << "Non-regular ISO source was accepted\n";
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        ::unlink(image_path);
        return 1;
    }
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);

    const std::string symlink_path = std::string(image_path) + ".link";
    if (::symlink(image_path, symlink_path.c_str()) != 0) {
        ::unlink(image_path);
        return 1;
    }
    iso_flash::ImageSnapshot symlink_snapshot;
    int symlink_fd = -1;
    const auto real_open = [](const char* path, int flags) {
        return ::open(path, flags);
    };
    if (iso_flash::open_image_read_only(symlink_path, symlink_fd, symlink_snapshot,
                                        error, real_open, real_fstat) ||
        symlink_fd >= 0) {
        std::cerr << "ISO symlink was accepted\n";
        if (symlink_fd >= 0) ::close(symlink_fd);
        ::unlink(symlink_path.c_str());
        ::unlink(image_path);
        return 1;
    }

    ::unlink(symlink_path.c_str());
    ::unlink(image_path);
    return 0;
}
