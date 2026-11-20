#pragma once

#include "image_hash.hpp"

#include <cerrno>
#include <functional>
#include <mutex>
#include <utility>

namespace iso_flash {

class ImageValidation {
public:
    explicit ImageValidation(ImageSnapshot expected_snapshot)
        : expected_snapshot_(expected_snapshot) {}

    int validate(int image_fd, const std::function<bool()>& cancel_requested) {
        ImageHashResult computed =
            hash_image_sha256(image_fd, expected_snapshot_, cancel_requested);
        int error_number = 0;
        switch (computed.status) {
        case ImageHashStatus::Completed:
            error_number = 0;
            break;
        case ImageHashStatus::Cancelled:
            error_number = ECANCELED;
            break;
        case ImageHashStatus::SourceChanged:
            error_number = ESTALE;
            break;
        case ImageHashStatus::Failed:
            error_number = computed.error_number == 0 ? EIO : computed.error_number;
            break;
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            result_ = std::move(computed);
            error_number_ = error_number;
        }
        return error_number;
    }

    ImageHashResult result() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return result_;
    }

    int error_number() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return error_number_;
    }

    bool is_valid() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return result_.status == ImageHashStatus::Completed &&
               !result_.sha256.empty() &&
               result_.bytes_hashed == expected_snapshot_.size_bytes;
    }

private:
    const ImageSnapshot expected_snapshot_;
    mutable std::mutex mutex_;
    ImageHashResult result_;
    int error_number_ = EIO;
};

}  // namespace iso_flash
