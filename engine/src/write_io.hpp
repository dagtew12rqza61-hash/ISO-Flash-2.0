#pragma once

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sys/types.h>

namespace iso_flash {

enum class WriteIoStatus {
    Completed,
    Cancelled,
    Failed
};

struct WriteIoResult {
    WriteIoStatus status = WriteIoStatus::Failed;
    std::size_t bytes_transferred = 0;
    int error_number = 0;
};

template <typename PwriteFunction, typename CancelFunction>
WriteIoResult write_all_at(int fd, const void* buffer, std::size_t size,
                           off_t offset, PwriteFunction&& pwrite_function,
                           CancelFunction&& cancel_requested) {
    if (offset < 0) {
        return {WriteIoStatus::Failed, 0, EINVAL};
    }

    const auto max_offset = std::numeric_limits<off_t>::max();
    if (size > static_cast<std::uintmax_t>(max_offset - offset)) {
        return {WriteIoStatus::Failed, 0, EOVERFLOW};
    }

    const auto* bytes = static_cast<const unsigned char*>(buffer);
    std::size_t transferred = 0;
    constexpr std::size_t max_chunk =
        static_cast<std::size_t>(std::numeric_limits<ssize_t>::max());

    while (transferred < size) {
        if (cancel_requested()) {
            return {WriteIoStatus::Cancelled, transferred, 0};
        }

        const std::size_t remaining = size - transferred;
        const std::size_t chunk = std::min(remaining, max_chunk);
        const off_t current_offset = offset + static_cast<off_t>(transferred);
        const ssize_t written = pwrite_function(
            fd, bytes + transferred, chunk, current_offset);

        if (written < 0) {
            const int write_error = errno;
            if (write_error == EINTR) {
                continue;
            }
            return {WriteIoStatus::Failed, transferred, write_error};
        }

        if (written == 0 || static_cast<std::size_t>(written) > chunk) {
            return {WriteIoStatus::Failed, transferred, EIO};
        }

        transferred += static_cast<std::size_t>(written);
    }

    return {WriteIoStatus::Completed, transferred, 0};
}

}  // namespace iso_flash
