#pragma once

#include <atomic>
#include <cstddef>
#include <utility>

namespace iso_flash {

class ImageValidationLimiter {
public:
    class Lease {
    public:
        Lease() noexcept = default;
        explicit Lease(ImageValidationLimiter* owner) noexcept : owner_(owner) {}

        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        Lease(Lease&& other) noexcept
            : owner_(std::exchange(other.owner_, nullptr)) {}

        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) {
                reset();
                owner_ = std::exchange(other.owner_, nullptr);
            }
            return *this;
        }

        ~Lease() { reset(); }

        explicit operator bool() const noexcept { return owner_ != nullptr; }

        void reset() noexcept {
            if (owner_ != nullptr) {
                owner_->release();
                owner_ = nullptr;
            }
        }

    private:
        ImageValidationLimiter* owner_ = nullptr;
    };

    explicit ImageValidationLimiter(unsigned int capacity) noexcept
        : capacity_(capacity) {}

    ImageValidationLimiter(const ImageValidationLimiter&) = delete;
    ImageValidationLimiter& operator=(const ImageValidationLimiter&) = delete;

    [[nodiscard]] Lease try_acquire() noexcept {
        unsigned int active = active_.load(std::memory_order_acquire);
        while (active < capacity_) {
            if (active_.compare_exchange_weak(
                    active, active + 1, std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return Lease(this);
            }
        }
        return {};
    }

    [[nodiscard]] unsigned int active() const noexcept {
        return active_.load(std::memory_order_acquire);
    }

    [[nodiscard]] unsigned int capacity() const noexcept { return capacity_; }

private:
    void release() noexcept {
        active_.fetch_sub(1, std::memory_order_acq_rel);
    }

    const unsigned int capacity_;
    std::atomic<unsigned int> active_{0};
};

}  // namespace iso_flash
