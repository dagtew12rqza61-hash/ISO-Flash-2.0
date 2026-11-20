#include "write_job_control.hpp"

#include <array>
#include <cerrno>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    iso_flash::WriteJobControl control(4096);
    if (!control.begin_validation() || !control.begin_writing()) {
        std::cerr << "Job did not enter writing state\n";
        return 1;
    }

    if (control.finish_completed()) {
        std::cerr << "Incomplete job was marked completed\n";
        return 1;
    }

    if (!control.update_progress(1024) ||
        control.update_progress(1023)) {
        std::cerr << "Progress synchronization accepted invalid data\n";
        return 1;
    }

    std::array<bool, 8> cancel_results{};
    std::vector<std::thread> threads;
    threads.reserve(cancel_results.size());
    for (std::size_t i = 0; i < cancel_results.size(); ++i) {
        threads.emplace_back([&control, &cancel_results, i] {
            cancel_results[i] = control.request_cancel();
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    std::size_t accepted = 0;
    for (const bool result : cancel_results) {
        accepted += result ? 1U : 0U;
    }
    if (accepted != 1 || !control.cancel_requested() ||
        control.update_progress(2048) || !control.finish_cancelled()) {
        std::cerr << "Concurrent cancellation was not serialized safely\n";
        return 1;
    }

    const auto cancelled = control.snapshot();
    if (cancelled.state != iso_flash::WriteJobState::Cancelled ||
        cancelled.completed_bytes != 1024 ||
        cancelled.total_bytes != 4096 || cancelled.error_number != 0) {
        std::cerr << "Cancelled job snapshot is inconsistent\n";
        return 1;
    }

    iso_flash::WriteJobControl completed(10);
    if (!completed.begin_validation() || !completed.begin_writing() ||
        !completed.update_progress(10) || !completed.finish_completed() ||
        completed.request_cancel()) {
        std::cerr << "Successful job lifecycle is invalid\n";
        return 1;
    }

    iso_flash::WriteJobControl failed(10);
    if (!failed.begin_validation() || !failed.finish_failed(EIO) ||
        failed.finish_failed(EIO) ||
        failed.snapshot().error_number != EIO) {
        std::cerr << "Failed job lifecycle is invalid\n";
        return 1;
    }

    iso_flash::WriteJobControl lost(10);
    if (!lost.begin_validation() || !lost.finish_device_lost(ENODEV) ||
        lost.snapshot().state != iso_flash::WriteJobState::DeviceLost ||
        lost.snapshot().error_number != ENODEV) {
        std::cerr << "Device-loss job lifecycle is invalid\n";
        return 1;
    }

    iso_flash::WriteJobControl invalid(0);
    if (invalid.begin_validation() ||
        invalid.snapshot().state != iso_flash::WriteJobState::Queued) {
        std::cerr << "Invalid zero-size job entered validation\n";
        return 1;
    }

    return 0;
}
