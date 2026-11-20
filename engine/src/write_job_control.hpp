#pragma once

#include "write_job_state.hpp"
#include "write_progress.hpp"

#include <cerrno>
#include <cstdint>
#include <mutex>

namespace iso_flash {

struct WriteJobSnapshot {
    WriteJobState state = WriteJobState::Queued;
    std::uint64_t completed_bytes = 0;
    std::uint64_t total_bytes = 0;
    int error_number = 0;
};

class WriteJobControl {
public:
    explicit WriteJobControl(std::uint64_t total_bytes) noexcept
        : progress_(total_bytes) {}

    WriteJobControl(const WriteJobControl&) = delete;
    WriteJobControl& operator=(const WriteJobControl&) = delete;

    bool begin_validation() noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        return progress_.valid() &&
               transition(state_, WriteJobState::Validating);
    }

    bool begin_writing() noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        return transition(state_, WriteJobState::Writing);
    }

    bool request_cancel() noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        return iso_flash::request_cancel(state_);
    }

    bool cancel_requested() const noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        return state_ == WriteJobState::Cancelling ||
               state_ == WriteJobState::Cancelled;
    }

    bool update_progress(std::uint64_t completed_bytes) noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        return state_ == WriteJobState::Writing &&
               progress_.update(completed_bytes);
    }

    bool finish_completed() noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        return progress_.complete() &&
               transition(state_, WriteJobState::Completed);
    }

    bool finish_cancelled() noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        return transition(state_, WriteJobState::Cancelled);
    }

    bool finish_failed(int error_number) noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (error_number == 0 ||
            !transition(state_, WriteJobState::Failed)) {
            return false;
        }
        error_number_ = error_number;
        return true;
    }

    bool finish_device_lost(int error_number) noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (error_number == 0 ||
            !transition(state_, WriteJobState::DeviceLost)) {
            return false;
        }
        error_number_ = error_number;
        return true;
    }

    WriteJobSnapshot snapshot() const noexcept {
        const std::lock_guard<std::mutex> lock(mutex_);
        return {state_, progress_.completed_bytes(),
                progress_.total_bytes(), error_number_};
    }

private:
    mutable std::mutex mutex_;
    WriteJobState state_ = WriteJobState::Queued;
    WriteProgress progress_;
    int error_number_ = 0;
};

}  // namespace iso_flash
