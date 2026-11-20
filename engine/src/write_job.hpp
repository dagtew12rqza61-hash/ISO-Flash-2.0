#pragma once

#include "device_io.hpp"
#include "write_job_state.hpp"

#include <cstdint>
#include <utility>

namespace iso_flash {

class WriteJob {
public:
    WriteJob(std::uint64_t id, UniqueFd image_fd, UniqueFd target_fd) noexcept
        : id_(id), image_fd_(std::move(image_fd)), target_fd_(std::move(target_fd)) {}

    WriteJob(const WriteJob&) = delete;
    WriteJob& operator=(const WriteJob&) = delete;
    WriteJob(WriteJob&&) noexcept = default;
    WriteJob& operator=(WriteJob&&) noexcept = default;

    std::uint64_t id() const noexcept { return id_; }
    WriteJobState state() const noexcept { return state_; }
    int image_fd() const noexcept { return image_fd_.get(); }
    int target_fd() const noexcept { return target_fd_.get(); }

    bool has_valid_resources() const noexcept {
        return id_ != 0 && image_fd_.get() >= 0 && target_fd_.get() >= 0;
    }

    bool transition_to(WriteJobState next) noexcept {
        return transition(state_, next);
    }

    bool cancel() noexcept {
        return request_cancel(state_);
    }

private:
    std::uint64_t id_;
    UniqueFd image_fd_;
    UniqueFd target_fd_;
    WriteJobState state_ = WriteJobState::Queued;
};

}  // namespace iso_flash
