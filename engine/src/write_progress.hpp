#pragma once

#include <cstdint>

namespace iso_flash {

class WriteProgress {
public:
    explicit constexpr WriteProgress(std::uint64_t total_bytes) noexcept
        : total_bytes_(total_bytes) {}

    constexpr bool valid() const noexcept {
        return total_bytes_ != 0;
    }

    constexpr bool update(std::uint64_t completed_bytes) noexcept {
        if (!valid() || completed_bytes < completed_bytes_ ||
            completed_bytes > total_bytes_) {
            return false;
        }
        completed_bytes_ = completed_bytes;
        return true;
    }

    constexpr std::uint64_t total_bytes() const noexcept {
        return total_bytes_;
    }

    constexpr std::uint64_t completed_bytes() const noexcept {
        return completed_bytes_;
    }

    constexpr std::uint64_t remaining_bytes() const noexcept {
        return total_bytes_ - completed_bytes_;
    }

    constexpr bool complete() const noexcept {
        return valid() && completed_bytes_ == total_bytes_;
    }

private:
    std::uint64_t total_bytes_ = 0;
    std::uint64_t completed_bytes_ = 0;
};

}  // namespace iso_flash
