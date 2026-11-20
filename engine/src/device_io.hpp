#pragma once

#include "device_error.hpp"

#include <cerrno>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace iso_flash {

class UniqueFd {
public:
    explicit UniqueFd(int fd = -1) noexcept : fd_(fd) {}
    ~UniqueFd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    int get() const noexcept { return fd_; }

    int release() noexcept {
        const int result = fd_;
        fd_ = -1;
        return result;
    }

    void reset(int fd = -1) noexcept {
        if (fd_ != fd) {
            if (fd_ >= 0) {
                ::close(fd_);
            }
            fd_ = fd;
        }
    }

private:
    int fd_;
};

template <typename OpenFunction>
bool open_target_read_only(const std::string& path, int& fd, std::string& error,
                           OpenFunction&& open_function) {
    fd = -1;
    do {
        fd = open_function(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    } while (fd < 0 && errno == EINTR);

    if (fd < 0) {
        const int open_error = errno;
        error = describe_device_error("Cannot open target read-only", open_error);
        return false;
    }
    return true;
}

}  // namespace iso_flash
