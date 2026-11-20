#pragma once

#include "device_io.hpp"
#include "image_source.hpp"
#include "target_source.hpp"
#include "writable_target.hpp"
#include "write_job_state.hpp"

#include <cstdint>
#include <utility>

namespace iso_flash {

class WriteJob {
public:
    WriteJob(std::uint64_t id, UniqueFd image_fd, UniqueFd target_fd) noexcept
        : id_(id), image_fd_(std::move(image_fd)), target_fd_(std::move(target_fd)) {}

    WriteJob(std::uint64_t id, OpenedImage image, UniqueFd target_fd) noexcept
        : id_(id), image_fd_(std::move(image.descriptor)),
          target_fd_(std::move(target_fd)), image_snapshot_(image.snapshot),
          has_image_snapshot_(image.snapshot.size_bytes > 0) {}

    WriteJob(std::uint64_t id, OpenedImage image, OpenedTarget target) noexcept
        : id_(id), image_fd_(std::move(image.descriptor)),
          target_fd_(std::move(target.descriptor)), image_snapshot_(image.snapshot),
          target_snapshot_(target.snapshot),
          has_image_snapshot_(image.snapshot.size_bytes > 0),
          has_target_snapshot_(target.snapshot.capacity_bytes > 0) {}

    WriteJob(std::uint64_t id, OpenedImage image, WritableTarget target) noexcept
        : id_(id), image_fd_(std::move(image.descriptor)),
          writable_target_(std::move(target)), image_snapshot_(image.snapshot),
          target_snapshot_(writable_target_.snapshot),
          has_image_snapshot_(image.snapshot.size_bytes > 0),
          has_target_snapshot_(writable_target_.snapshot.capacity_bytes > 0) {}

    WriteJob(const WriteJob&) = delete;
    WriteJob& operator=(const WriteJob&) = delete;
    WriteJob(WriteJob&&) noexcept = default;
    WriteJob& operator=(WriteJob&&) noexcept = default;

    std::uint64_t id() const noexcept { return id_; }
    WriteJobState state() const noexcept { return state_; }
    int image_fd() const noexcept { return image_fd_.get(); }
    int target_fd() const noexcept {
        return writable_target_.descriptor.get() >= 0
            ? writable_target_.descriptor.get() : target_fd_.get();
    }
    bool has_image_snapshot() const noexcept { return has_image_snapshot_; }
    const ImageSnapshot& image_snapshot() const noexcept { return image_snapshot_; }
    bool has_target_snapshot() const noexcept { return has_target_snapshot_; }
    const DeviceSnapshot& target_snapshot() const noexcept { return target_snapshot_; }
    const WritableTarget& writable_target() const noexcept { return writable_target_; }

    bool has_valid_resources() const noexcept {
        return id_ != 0 && image_fd_.get() >= 0 && target_fd() >= 0;
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
    WritableTarget writable_target_;
    ImageSnapshot image_snapshot_{};
    DeviceSnapshot target_snapshot_{};
    bool has_image_snapshot_ = false;
    bool has_target_snapshot_ = false;
    WriteJobState state_ = WriteJobState::Queued;
};

}  // namespace iso_flash
