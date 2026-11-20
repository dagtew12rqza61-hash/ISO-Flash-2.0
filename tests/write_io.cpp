#include "write_io.hpp"

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

int main() {
    const std::string payload = "ISO-FLASH-WRITE-TEST";
    std::string output;
    int calls = 0;
    const auto short_writer = [&](int, const void* data, std::size_t size, off_t offset) -> ssize_t {
        ++calls;
        const std::size_t count = std::min<std::size_t>(size, 3);
        const auto* chars = static_cast<const char*>(data);
        if (offset < 0 || static_cast<std::size_t>(offset) != output.size()) {
            errno = EINVAL;
            return -1;
        }
        output.append(chars, count);
        return static_cast<ssize_t>(count);
    };
    const auto never_cancel = [] { return false; };
    const auto completed = iso_flash::write_all_at(
        9, payload.data(), payload.size(), 0, short_writer, never_cancel);
    if (completed.status != iso_flash::WriteIoStatus::Completed ||
        completed.bytes_transferred != payload.size() || output != payload ||
        calls <= 1) {
        std::cerr << "Short writes were not completed correctly\n";
        return 1;
    }

    calls = 0;
    const auto interrupted_writer = [&](int, const void*, std::size_t size, off_t) -> ssize_t {
        ++calls;
        if (calls == 1) {
            errno = EINTR;
            return -1;
        }
        return static_cast<ssize_t>(size);
    };
    const auto retried = iso_flash::write_all_at(
        9, payload.data(), payload.size(), 0, interrupted_writer, never_cancel);
    if (retried.status != iso_flash::WriteIoStatus::Completed ||
        retried.bytes_transferred != payload.size() || calls != 2) {
        std::cerr << "EINTR was not retried correctly\n";
        return 1;
    }

    calls = 0;
    const auto partial_then_cancel = [&](int, const void*, std::size_t size, off_t) -> ssize_t {
        ++calls;
        return static_cast<ssize_t>(std::min<std::size_t>(size, 4));
    };
    const auto cancel_after_first = [&] { return calls > 0; };
    const auto cancelled = iso_flash::write_all_at(
        9, payload.data(), payload.size(), 0, partial_then_cancel, cancel_after_first);
    if (cancelled.status != iso_flash::WriteIoStatus::Cancelled ||
        cancelled.bytes_transferred != 4 || calls != 1) {
        std::cerr << "Cancellation did not stop after the in-flight partial write\n";
        return 1;
    }

    const auto no_space_writer = [](int, const void*, std::size_t, off_t) -> ssize_t {
        errno = ENOSPC;
        return -1;
    };
    const auto no_space = iso_flash::write_all_at(
        9, payload.data(), payload.size(), 0, no_space_writer, never_cancel);
    if (no_space.status != iso_flash::WriteIoStatus::Failed ||
        no_space.error_number != ENOSPC || no_space.bytes_transferred != 0) {
        std::cerr << "ENOSPC was not reported correctly\n";
        return 1;
    }

    const auto zero_writer = [](int, const void*, std::size_t, off_t) -> ssize_t {
        return 0;
    };
    const auto zero = iso_flash::write_all_at(
        9, payload.data(), payload.size(), 0, zero_writer, never_cancel);
    if (zero.status != iso_flash::WriteIoStatus::Failed || zero.error_number != EIO) {
        std::cerr << "Zero-length write was not rejected\n";
        return 1;
    }

    const auto invalid = iso_flash::write_all_at(
        9, payload.data(), payload.size(), static_cast<off_t>(-1),
        zero_writer, never_cancel);
    if (invalid.status != iso_flash::WriteIoStatus::Failed ||
        invalid.error_number != EINVAL) {
        std::cerr << "Negative offset was not rejected\n";
        return 1;
    }

    const auto overflow = iso_flash::write_all_at(
        9, payload.data(), payload.size(), std::numeric_limits<off_t>::max(),
        zero_writer, never_cancel);
    if (overflow.status != iso_flash::WriteIoStatus::Failed ||
        overflow.error_number != EOVERFLOW) {
        std::cerr << "Offset overflow was not rejected\n";
        return 1;
    }

    return 0;
}
