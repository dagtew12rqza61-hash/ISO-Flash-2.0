#pragma once

#include "image_source.hpp"
#include "image_hash.hpp"
#include "write_io.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>

namespace iso_flash {

struct VerifiedWriteResult {
    WriteIoStatus status = WriteIoStatus::Failed;
    std::uint64_t bytes_transferred = 0;
    int error_number = 0;
    bool verified = false;
    std::string readback_sha256;
};

inline ImageHashResult hash_fd_range_sha256(
    int fd, std::uint64_t size,
    const std::function<bool()>& cancel_requested,
    const std::function<ssize_t(int, void*, std::size_t, off_t)>& pread_function =
        [](int descriptor, void* buffer, std::size_t count, off_t offset) {
            return ::pread(descriptor, buffer, count, offset);
        }) {
    ImageHashResult result;
    if (fd < 0 || size == 0 ||
        size > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        result.error_number = EINVAL;
        result.error = "Invalid descriptor or byte range for SHA-256";
        return result;
    }

    GChecksum* checksum = g_checksum_new(G_CHECKSUM_SHA256);
    if (checksum == nullptr) {
        result.error_number = ENOMEM;
        result.error = "Cannot initialize SHA-256";
        return result;
    }

    constexpr std::size_t chunk_size = 1024U * 1024U;
    std::array<unsigned char, chunk_size> buffer{};
    while (result.bytes_hashed < size) {
        if (cancel_requested && cancel_requested()) {
            result.status = ImageHashStatus::Cancelled;
            result.error = "Read-back hashing cancelled";
            g_checksum_free(checksum);
            return result;
        }

        const std::uint64_t remaining = size - result.bytes_hashed;
        const std::size_t requested = static_cast<std::size_t>(
            remaining < static_cast<std::uint64_t>(buffer.size())
                ? remaining : static_cast<std::uint64_t>(buffer.size()));
        ssize_t count = -1;
        do {
            count = pread_function(fd, buffer.data(), requested,
                                   static_cast<off_t>(result.bytes_hashed));
        } while (count < 0 && errno == EINTR);

        if (count < 0) {
            result.error_number = errno == 0 ? EIO : errno;
            result.error = "Cannot read data for SHA-256 verification";
            g_checksum_free(checksum);
            return result;
        }
        if (count == 0 || static_cast<std::size_t>(count) > requested) {
            result.error_number = EIO;
            result.error = "Read-back ended early or returned an invalid byte count";
            g_checksum_free(checksum);
            return result;
        }

        g_checksum_update(checksum, buffer.data(), static_cast<gssize>(count));
        result.bytes_hashed += static_cast<std::uint64_t>(count);
    }

    if (cancel_requested && cancel_requested()) {
        result.status = ImageHashStatus::Cancelled;
        result.error = "Read-back hashing cancelled";
        g_checksum_free(checksum);
        return result;
    }

    const gchar* digest = g_checksum_get_string(checksum);
    if (digest == nullptr) {
        result.error_number = EIO;
        result.error = "Cannot finalize read-back SHA-256";
        g_checksum_free(checksum);
        return result;
    }
    result.sha256 = digest;
    result.status = ImageHashStatus::Completed;
    g_checksum_free(checksum);
    return result;
}

inline VerifiedWriteResult write_image_and_verify(
    int image_fd, int target_fd, const ImageSnapshot& image_snapshot,
    std::uint64_t target_capacity, const std::string& expected_sha256,
    const std::function<int()>& revalidate_before_first_write,
    const std::function<bool(std::uint64_t)>& report_progress,
    const std::function<bool()>& cancel_requested,
    const std::function<ssize_t(int, void*, std::size_t, off_t)>& pread_function =
        [](int descriptor, void* buffer, std::size_t count, off_t offset) {
            return ::pread(descriptor, buffer, count, offset);
        },
    const std::function<ssize_t(int, const void*, std::size_t, off_t)>& pwrite_function =
        [](int descriptor, const void* buffer, std::size_t count, off_t offset) {
            return ::pwrite(descriptor, buffer, count, offset);
        },
    const std::function<int(int)>& fsync_function =
        [](int descriptor) { return ::fsync(descriptor); }) {
    VerifiedWriteResult result;
    if (image_fd < 0 || target_fd < 0 || image_snapshot.size_bytes == 0 ||
        image_snapshot.size_bytes > target_capacity ||
        expected_sha256.size() != 64) {
        result.error_number = EINVAL;
        return result;
    }

    ImageSnapshot before{};
    std::string snapshot_error;
    if (!read_image_snapshot(image_fd, before, snapshot_error,
                             [](int descriptor, struct stat* metadata) {
                                 return ::fstat(descriptor, metadata);
                             }) ||
        !same_image_snapshot(image_snapshot, before)) {
        result.error_number = ESTALE;
        return result;
    }

    if (cancel_requested && cancel_requested()) {
        result.status = WriteIoStatus::Cancelled;
        return result;
    }
    const int validation_error = revalidate_before_first_write
        ? revalidate_before_first_write() : EINVAL;
    if (validation_error != 0) {
        result.error_number = validation_error;
        return result;
    }
    if (cancel_requested && cancel_requested()) {
        result.status = WriteIoStatus::Cancelled;
        return result;
    }

    result.status = WriteIoStatus::Completed;
    constexpr std::size_t chunk_size = 1024U * 1024U;
    std::array<unsigned char, chunk_size> buffer{};
    while (result.bytes_transferred < image_snapshot.size_bytes) {
        if (cancel_requested && cancel_requested()) {
            result.status = WriteIoStatus::Cancelled;
            break;
        }

        const std::uint64_t remaining =
            image_snapshot.size_bytes - result.bytes_transferred;
        const std::size_t requested = static_cast<std::size_t>(
            remaining < static_cast<std::uint64_t>(buffer.size())
                ? remaining : static_cast<std::uint64_t>(buffer.size()));
        std::size_t read_bytes = 0;
        while (read_bytes < requested) {
            if (cancel_requested && cancel_requested()) {
                result.status = WriteIoStatus::Cancelled;
                break;
            }
            ssize_t count = -1;
            do {
                count = pread_function(
                    image_fd, buffer.data() + read_bytes,
                    requested - read_bytes,
                    static_cast<off_t>(result.bytes_transferred + read_bytes));
            } while (count < 0 && errno == EINTR);
            if (count <= 0 ||
                static_cast<std::size_t>(count) > requested - read_bytes) {
                result.error_number = count < 0 && errno != 0 ? errno : EIO;
                result.status = WriteIoStatus::Failed;
                break;
            }
            read_bytes += static_cast<std::size_t>(count);
        }
        if (result.status != WriteIoStatus::Failed &&
            result.status != WriteIoStatus::Cancelled) {
            const WriteIoResult write_result = write_all_at(
                target_fd, buffer.data(), requested,
                static_cast<off_t>(result.bytes_transferred),
                pwrite_function,
                [&cancel_requested] {
                    return cancel_requested && cancel_requested();
                });
            result.bytes_transferred +=
                static_cast<std::uint64_t>(write_result.bytes_transferred);
            if (write_result.status == WriteIoStatus::Failed) {
                result.status = WriteIoStatus::Failed;
                result.error_number = write_result.error_number == 0
                    ? EIO : write_result.error_number;
            } else if (write_result.status == WriteIoStatus::Cancelled) {
                result.status = WriteIoStatus::Cancelled;
            } else if (report_progress &&
                       !report_progress(result.bytes_transferred)) {
                result.status = WriteIoStatus::Failed;
                result.error_number = EIO;
            }
        }
        if (result.status == WriteIoStatus::Failed ||
            result.status == WriteIoStatus::Cancelled) {
            break;
        }
    }

    const auto sync_target = [&] {
        int sync_result = -1;
        do {
            sync_result = fsync_function(target_fd);
        } while (sync_result != 0 && errno == EINTR);
        return sync_result == 0 ? 0 : (errno == 0 ? EIO : errno);
    };
    const auto probe_device_loss = [&] {
        if (!revalidate_before_first_write) return;
        const int validation_error = revalidate_before_first_write();
        if (validation_error == ENODEV || validation_error == ENXIO ||
            validation_error == ENOMEDIUM) {
            result.status = WriteIoStatus::Failed;
            result.error_number = validation_error;
        }
    };

    if (result.status == WriteIoStatus::Failed ||
        result.status == WriteIoStatus::Cancelled) {
        const int sync_error = sync_target();
        if (sync_error != 0) {
            result.status = WriteIoStatus::Failed;
            result.error_number = sync_error;
        }
        probe_device_loss();
        return result;
    }

    const int sync_error = sync_target();
    if (sync_error != 0) {
        result.status = WriteIoStatus::Failed;
        result.error_number = sync_error;
        probe_device_loss();
        return result;
    }

    ImageSnapshot after{};
    snapshot_error.clear();
    if (!read_image_snapshot(image_fd, after, snapshot_error,
                             [](int descriptor, struct stat* metadata) {
                                 return ::fstat(descriptor, metadata);
                             }) ||
        !same_image_snapshot(image_snapshot, after)) {
        result.status = WriteIoStatus::Failed;
        result.error_number = ESTALE;
        return result;
    }

    const ImageHashResult readback = hash_fd_range_sha256(
        target_fd, image_snapshot.size_bytes, cancel_requested, pread_function);
    if (readback.status == ImageHashStatus::Cancelled) {
        result.status = WriteIoStatus::Cancelled;
        return result;
    }
    if (readback.status != ImageHashStatus::Completed) {
        result.status = WriteIoStatus::Failed;
        result.error_number = readback.error_number == 0 ? EIO : readback.error_number;
        probe_device_loss();
        return result;
    }

    result.readback_sha256 = readback.sha256;
    if (result.readback_sha256 != expected_sha256) {
        result.status = WriteIoStatus::Failed;
        result.error_number = EILSEQ;
        return result;
    }

    result.status = WriteIoStatus::Completed;
    result.verified = true;
    return result;
}

}  // namespace iso_flash
