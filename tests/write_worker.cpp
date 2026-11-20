#include "write_worker.hpp"

#include <cerrno>
#include <future>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <utility>

namespace {

iso_flash::WriteJob make_job(std::uint64_t id) {
    int image_pipe[2] = {-1, -1};
    int target_pipe[2] = {-1, -1};
    if (::pipe(image_pipe) != 0 || ::pipe(target_pipe) != 0) {
        if (image_pipe[0] >= 0) {
            ::close(image_pipe[0]);
            ::close(image_pipe[1]);
        }
        if (target_pipe[0] >= 0) {
            ::close(target_pipe[0]);
            ::close(target_pipe[1]);
        }
        throw std::runtime_error("pipe creation failed");
    }
    ::close(image_pipe[1]);
    ::close(target_pipe[1]);
    return iso_flash::WriteJob(
        id, iso_flash::UniqueFd(image_pipe[0]),
        iso_flash::UniqueFd(target_pipe[0]));
}

bool expect_state(const iso_flash::WriteJobSnapshot& snapshot,
                 iso_flash::WriteJobState state,
                 std::uint64_t completed,
                 int error_number = 0) {
    return snapshot.state == state &&
           snapshot.completed_bytes == completed &&
           snapshot.error_number == error_number;
}

}  // namespace

int main() {
    {
        iso_flash::WriteWorker worker(
            make_job(1), 100,
            [](iso_flash::WriteJob&, const iso_flash::WriteWorker::CancelCallback&) { return 0; },
            [](iso_flash::WriteJob&,
               const iso_flash::WriteWorker::ProgressCallback& progress,
               const iso_flash::WriteWorker::CancelCallback&) {
                if (!progress(40) || !progress(100)) {
                    return iso_flash::WriteOperationResult{
                        iso_flash::WriteIoStatus::Failed, 40, EIO};
                }
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Completed, 100, 0};
            });
        if (!worker.start() || worker.start()) {
            std::cerr << "Worker start guard failed\n";
            return 1;
        }
        worker.join();
        if (!expect_state(worker.snapshot(), iso_flash::WriteJobState::Completed, 100)) {
            std::cerr << "Successful asynchronous job did not complete\n";
            return 1;
        }
    }

    {
        std::promise<void> entered;
        std::promise<void> release;
        const auto release_signal = release.get_future().share();
        iso_flash::WriteWorker worker(
            make_job(2), 100,
            [](iso_flash::WriteJob&, const iso_flash::WriteWorker::CancelCallback&) { return 0; },
            [&entered, release_signal](
                iso_flash::WriteJob&,
                const iso_flash::WriteWorker::ProgressCallback& progress,
                const iso_flash::WriteWorker::CancelCallback& cancelled) {
                progress(25);
                entered.set_value();
                release_signal.wait();
                if (cancelled()) {
                    return iso_flash::WriteOperationResult{
                        iso_flash::WriteIoStatus::Cancelled, 25, 0};
                }
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Completed, 100, 0};
            });
        if (!worker.start()) {
            std::cerr << "Cancellation worker failed to start\n";
            return 1;
        }
        entered.get_future().wait();
        if (!worker.cancel()) {
            std::cerr << "Running job rejected cancellation\n";
            return 1;
        }
        release.set_value();
        worker.join();
        if (!expect_state(worker.snapshot(), iso_flash::WriteJobState::Cancelled, 25)) {
            std::cerr << "Cancellation did not preserve partial progress\n";
            return 1;
        }
    }

    {
        std::promise<void> entered;
        std::promise<void> release;
        const auto release_signal = release.get_future().share();
        iso_flash::WriteWorker worker(
            make_job(11), 100,
            [](iso_flash::WriteJob&, const iso_flash::WriteWorker::CancelCallback&) { return 0; },
            [&entered, release_signal](
                iso_flash::WriteJob&,
                const iso_flash::WriteWorker::ProgressCallback& progress,
                const iso_flash::WriteWorker::CancelCallback& cancelled) {
                if (!progress(20)) {
                    return iso_flash::WriteOperationResult{
                        iso_flash::WriteIoStatus::Failed, 0, EIO};
                }
                entered.set_value();
                release_signal.wait();
                return iso_flash::WriteOperationResult{
                    cancelled() ? iso_flash::WriteIoStatus::Cancelled
                                : iso_flash::WriteIoStatus::Completed,
                    37, 0};
            });
        if (!worker.start()) {
            std::cerr << "Partial-cancel worker failed to start\\n";
            return 1;
        }
        entered.get_future().wait();
        if (!worker.cancel()) {
            std::cerr << "Partial-cancel worker rejected cancellation\\n";
            return 1;
        }
        release.set_value();
        worker.join();
        if (!expect_state(worker.snapshot(), iso_flash::WriteJobState::Cancelled, 37)) {
            std::cerr << "Cancellation lost bytes written inside an interrupted chunk\\n";
            return 1;
        }
    }

    {
        iso_flash::WriteWorker worker(
            make_job(3), 100,
            [](iso_flash::WriteJob&, const iso_flash::WriteWorker::CancelCallback&) { return 0; },
            [](iso_flash::WriteJob&,
               const iso_flash::WriteWorker::ProgressCallback&,
               const iso_flash::WriteWorker::CancelCallback&) {
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Failed, 0, ENOSPC};
            });
        if (!worker.start()) {
            std::cerr << "Failure worker failed to start\n";
            return 1;
        }
        worker.join();
        if (!expect_state(worker.snapshot(), iso_flash::WriteJobState::Failed, 0, ENOSPC)) {
            std::cerr << "Ordinary I/O failure was not preserved\n";
            return 1;
        }
    }

    {
        iso_flash::WriteWorker worker(
            make_job(4), 100,
            [](iso_flash::WriteJob&, const iso_flash::WriteWorker::CancelCallback&) { return 0; },
            [](iso_flash::WriteJob&,
               const iso_flash::WriteWorker::ProgressCallback&,
               const iso_flash::WriteWorker::CancelCallback&) {
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Failed, 0, ENODEV};
            });
        if (!worker.start()) {
            std::cerr << "Device-loss worker failed to start\n";
            return 1;
        }
        worker.join();
        if (!expect_state(worker.snapshot(), iso_flash::WriteJobState::DeviceLost, 0, ENODEV)) {
            std::cerr << "Device loss was not classified as terminal\n";
            return 1;
        }
    }

    {
        iso_flash::WriteWorker worker(
            make_job(5), 100,
            [](iso_flash::WriteJob&, const iso_flash::WriteWorker::CancelCallback&) { return 0; },
            [](iso_flash::WriteJob&,
               const iso_flash::WriteWorker::ProgressCallback&,
               const iso_flash::WriteWorker::CancelCallback&) -> iso_flash::WriteOperationResult {
                throw std::runtime_error("injected worker exception");
            });
        if (!worker.start()) {
            std::cerr << "Exception worker failed to start\n";
            return 1;
        }
        worker.join();
        if (!expect_state(worker.snapshot(), iso_flash::WriteJobState::Failed, 0, EIO)) {
            std::cerr << "Worker exception escaped failure handling\n";
            return 1;
        }
    }


    {
        iso_flash::WriteWorker worker(
            make_job(6), 100,
            [](iso_flash::WriteJob&, const iso_flash::WriteWorker::CancelCallback&) { return 0; },
            [](iso_flash::WriteJob&,
               const iso_flash::WriteWorker::ProgressCallback& progress,
               const iso_flash::WriteWorker::CancelCallback&) {
                progress(25);
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Failed, 0, ENOSPC};
            });
        if (!worker.start()) {
            std::cerr << "Partial-failure worker failed to start\n";
            return 1;
        }
        worker.join();
        if (!expect_state(worker.snapshot(), iso_flash::WriteJobState::Failed, 25, ENOSPC)) {
            std::cerr << "Partial progress overwrote the underlying I/O error\n";
            return 1;
        }
    }

    {
        iso_flash::WriteWorker worker(
            make_job(7), 100,
            [](iso_flash::WriteJob&, const iso_flash::WriteWorker::CancelCallback&) { return 0; },
            [](iso_flash::WriteJob&,
               const iso_flash::WriteWorker::ProgressCallback&,
               const iso_flash::WriteWorker::CancelCallback&) {
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Completed, 101, 0};
            });
        if (!worker.start()) {
            std::cerr << "Overrun worker failed to start\n";
            return 1;
        }
        worker.join();
        if (!expect_state(worker.snapshot(), iso_flash::WriteJobState::Failed, 0, EIO)) {
            std::cerr << "Worker accepted a byte count beyond the image size\n";
            return 1;
        }
    }

    {
        iso_flash::WriteWorker worker(
            make_job(8), 100,
            [](iso_flash::WriteJob&, const iso_flash::WriteWorker::CancelCallback&) { return 0; },
            [](iso_flash::WriteJob&,
               const iso_flash::WriteWorker::ProgressCallback&,
               const iso_flash::WriteWorker::CancelCallback&) {
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Completed, 100, 0};
            });
        if (!worker.cancel() || !worker.start()) {
            std::cerr << "Queued cancellation was not accepted\n";
            return 1;
        }
        worker.join();
        if (!expect_state(worker.snapshot(), iso_flash::WriteJobState::Cancelled, 0)) {
            std::cerr << "Queued cancellation unexpectedly ran the operation\n";
            return 1;
        }
    }


    {
        std::promise<void> validating;
        std::promise<void> finish_validation;
        const auto finish_signal = finish_validation.get_future().share();
        iso_flash::WriteWorker worker(
            make_job(9), 100,
            [&validating, finish_signal](iso_flash::WriteJob&, const iso_flash::WriteWorker::CancelCallback& cancelled) {
                validating.set_value();
                finish_signal.wait();
                return cancelled() ? ECANCELED : 0;
            },
            [](iso_flash::WriteJob&,
               const iso_flash::WriteWorker::ProgressCallback&,
               const iso_flash::WriteWorker::CancelCallback&) {
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Completed, 100, 0};
            });
        if (!worker.start()) {
            std::cerr << "Validation cancellation worker failed to start\n";
            return 1;
        }
        validating.get_future().wait();
        if (!worker.cancel()) {
            std::cerr << "Validating job rejected cancellation\n";
            return 1;
        }
        finish_validation.set_value();
        worker.join();
        if (!expect_state(worker.snapshot(), iso_flash::WriteJobState::Cancelled, 0)) {
            std::cerr << "Cancelled validation proceeded into writing\n";
            return 1;
        }
    }

    {
        iso_flash::WriteWorker worker(
            make_job(10), 100,
            [](iso_flash::WriteJob&, const iso_flash::WriteWorker::CancelCallback&) { return ENODEV; },
            [](iso_flash::WriteJob&,
               const iso_flash::WriteWorker::ProgressCallback&,
               const iso_flash::WriteWorker::CancelCallback&) {
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Completed, 100, 0};
            });
        if (!worker.start()) {
            std::cerr << "Validation failure worker failed to start\n";
            return 1;
        }
        worker.join();
        if (!expect_state(worker.snapshot(), iso_flash::WriteJobState::DeviceLost, 0, ENODEV)) {
            std::cerr << "Validation did not classify device loss\n";
            return 1;
        }
    }

    return 0;
}
