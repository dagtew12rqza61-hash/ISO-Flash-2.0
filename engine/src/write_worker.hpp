#pragma once

#include "write_io.hpp"
#include "write_job.hpp"
#include "write_job_control.hpp"

#include <cerrno>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace iso_flash {

struct WriteOperationResult {
    WriteIoStatus status = WriteIoStatus::Failed;
    std::uint64_t bytes_transferred = 0;
    int error_number = 0;
};

class WriteWorker {
public:
    using ProgressCallback = std::function<bool(std::uint64_t)>;
    using CancelCallback = std::function<bool()>;
    using Validation = std::function<int(WriteJob&, const CancelCallback&)>;
    using Operation = std::function<WriteOperationResult(
        WriteJob&, const ProgressCallback&, const CancelCallback&)>;

    WriteWorker(WriteJob job, std::uint64_t total_bytes,
                Validation validation, Operation operation)
        : job_(std::move(job)),
          control_(total_bytes),
          validation_(std::move(validation)),
          operation_(std::move(operation)) {}

    WriteWorker(const WriteWorker&) = delete;
    WriteWorker& operator=(const WriteWorker&) = delete;
    WriteWorker(WriteWorker&&) = delete;
    WriteWorker& operator=(WriteWorker&&) = delete;

    std::uint64_t id() const noexcept { return job_.id(); }

    ~WriteWorker() {
        cancel();
        join();
    }

    bool start() {
        const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (started_ || !job_.has_valid_resources() || !validation_ || !operation_ ||
            !control_.snapshot().total_bytes) {
            return false;
        }
        try {
            worker_ = std::thread([this] { run(); });
        } catch (...) {
            return false;
        }
        started_ = true;
        return true;
    }

    bool cancel() {
        return control_.request_cancel();
    }

    void join() {
        const std::lock_guard<std::mutex> lock(lifecycle_mutex_);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    WriteJobSnapshot snapshot() const {
        return control_.snapshot();
    }

private:
    static bool is_device_lost(int error_number) noexcept {
        return error_number == ENODEV || error_number == ENXIO ||
               error_number == ENOMEDIUM;
    }

    void finalize_cancelled() {
        control_.finish_cancelled();
    }

    void run() noexcept {
        try {
            if (!control_.begin_validation()) {
                return;
            }

            int validation_error = 0;
            try {
                validation_error = validation_(job_, [this] { return control_.cancel_requested(); });
            } catch (...) {
                validation_error = EIO;
            }
            if (validation_error != 0) {
                if (control_.cancel_requested()) {
                    finalize_cancelled();
                } else if (is_device_lost(validation_error)) {
                    control_.finish_device_lost(validation_error);
                } else {
                    control_.finish_failed(validation_error);
                }
                return;
            }

            if (control_.cancel_requested()) {
                finalize_cancelled();
                return;
            }

            if (!control_.begin_writing()) {
                if (control_.cancel_requested()) {
                    finalize_cancelled();
                } else {
                    control_.finish_failed(EIO);
                }
                return;
            }

            const ProgressCallback report_progress =
                [this](std::uint64_t completed) {
                    return control_.update_progress(completed);
                };
            const CancelCallback cancellation_requested =
                [this] { return control_.cancel_requested(); };

            WriteOperationResult result;
            try {
                result = operation_(job_, report_progress, cancellation_requested);
            } catch (...) {
                result = {WriteIoStatus::Failed, 0, EIO};
            }

            const WriteJobSnapshot current = control_.snapshot();
            if (result.bytes_transferred > current.total_bytes) {
                control_.finish_failed(EIO);
                return;
            }

            if (result.status == WriteIoStatus::Cancelled) {
                if (!control_.cancel_requested()) {
                    control_.request_cancel();
                }
                finalize_cancelled();
                return;
            }

            if (result.status == WriteIoStatus::Failed) {
                const int error_number =
                    result.error_number == 0 ? EIO : result.error_number;
                if (is_device_lost(error_number)) {
                    control_.finish_device_lost(error_number);
                } else {
                    control_.finish_failed(error_number);
                }
                return;
            }

            if (control_.cancel_requested()) {
                finalize_cancelled();
                return;
            }

            if (result.bytes_transferred != current.total_bytes ||
                !control_.update_progress(result.bytes_transferred) ||
                !control_.finish_completed()) {
                if (control_.cancel_requested()) {
                    finalize_cancelled();
                } else {
                    control_.finish_failed(EIO);
                }
            }
        } catch (...) {
            control_.finish_failed(EIO);
        }
    }

    WriteJob job_;
    WriteJobControl control_;
    Validation validation_;
    Operation operation_;
    mutable std::mutex lifecycle_mutex_;
    std::thread worker_;
    bool started_ = false;
};

}  // namespace iso_flash
