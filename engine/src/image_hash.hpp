#pragma once

#include "image_source.hpp"

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

enum class ImageHashStatus {
    Completed,
    Cancelled,
    Failed,
    SourceChanged
};

struct ImageHashResult {
    ImageHashStatus status = ImageHashStatus::Failed;
    std::string sha256;
    std::uint64_t bytes_hashed = 0;
    int error_number = 0;
    std::string error;
};

inline ImageHashResult hash_image_sha256(
    int fd, const ImageSnapshot& expected_snapshot,
    const std::function<bool()>& cancel_requested,
    const std::function<ssize_t(int, void*, std::size_t, off_t)>& pread_function =
        [](int descriptor, void* buffer, std::size_t size, off_t offset) {
            return ::pread(descriptor, buffer, size, offset);
        }) {
    ImageHashResult result;
    if (fd < 0 || expected_snapshot.size_bytes == 0) {
        result.error = "Invalid ISO descriptor or empty snapshot";
        result.error_number = EINVAL;
        return result;
    }
    if (expected_snapshot.size_bytes >
        static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
        result.error = "ISO source exceeds supported file offset range";
        result.error_number = EOVERFLOW;
        return result;
    }

    ImageSnapshot before;
    std::string snapshot_error;
    if (!read_image_snapshot(fd, before, snapshot_error,
                             [](int descriptor, struct stat* metadata) {
                                 return ::fstat(descriptor, metadata);
                             }) ||
        !same_image_snapshot(expected_snapshot, before)) {
        result.status = ImageHashStatus::SourceChanged;
        result.error = snapshot_error.empty()
                           ? "ISO source changed before hashing"
                           : snapshot_error;
        return result;
    }

    GChecksum* checksum = g_checksum_new(G_CHECKSUM_SHA256);
    if (checksum == nullptr) {
        result.error = "Cannot initialize SHA-256";
        result.error_number = ENOMEM;
        return result;
    }

    constexpr std::size_t chunk_size = 1024U * 1024U;
    std::array<unsigned char, chunk_size> buffer{};
    while (result.bytes_hashed < expected_snapshot.size_bytes) {
        if (cancel_requested && cancel_requested()) {
            result.status = ImageHashStatus::Cancelled;
            result.error = "ISO hashing cancelled";
            g_checksum_free(checksum);
            return result;
        }

        const std::uint64_t remaining =
            expected_snapshot.size_bytes - result.bytes_hashed;
        const std::size_t requested = static_cast<std::size_t>(
            remaining < static_cast<std::uint64_t>(buffer.size())
                ? remaining
                : static_cast<std::uint64_t>(buffer.size()));
        const off_t offset = static_cast<off_t>(result.bytes_hashed);
        ssize_t count = -1;
        do {
            count = pread_function(fd, buffer.data(), requested, offset);
        } while (count < 0 && errno == EINTR);

        if (count < 0) {
            result.error_number = errno;
            result.error = "Cannot read ISO source while hashing (errno " +
                           std::to_string(result.error_number) + ")";
            g_checksum_free(checksum);
            return result;
        }
        if (count == 0) {
            result.error_number = EIO;
            result.error = "ISO source ended before its recorded size";
            g_checksum_free(checksum);
            return result;
        }

        const auto received = static_cast<std::size_t>(count);
        if (received > requested) {
            result.error_number = EIO;
            result.error = "ISO read returned more bytes than requested";
            g_checksum_free(checksum);
            return result;
        }
        g_checksum_update(checksum, buffer.data(), static_cast<gssize>(received));
        result.bytes_hashed += static_cast<std::uint64_t>(received);
    }

    if (cancel_requested && cancel_requested()) {
        result.status = ImageHashStatus::Cancelled;
        result.error = "ISO hashing cancelled";
        g_checksum_free(checksum);
        return result;
    }

    ImageSnapshot after;
    snapshot_error.clear();
    if (!read_image_snapshot(fd, after, snapshot_error,
                             [](int descriptor, struct stat* metadata) {
                                 return ::fstat(descriptor, metadata);
                             }) ||
        !same_image_snapshot(expected_snapshot, after)) {
        result.status = ImageHashStatus::SourceChanged;
        result.error = snapshot_error.empty()
                           ? "ISO source changed while hashing"
                           : snapshot_error;
        g_checksum_free(checksum);
        return result;
    }

    const gchar* digest = g_checksum_get_string(checksum);
    if (digest == nullptr) {
        result.error = "Cannot finalize SHA-256";
        result.error_number = EIO;
        g_checksum_free(checksum);
        return result;
    }
    result.sha256 = digest;
    result.status = ImageHashStatus::Completed;
    g_checksum_free(checksum);
    return result;
}

}  // namespace iso_flash
