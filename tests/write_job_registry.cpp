#include "write_job_registry.hpp"

#include <cerrno>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <unistd.h>

namespace {

iso_flash::WriteJob make_job(std::uint64_t id) {
    int image_pipe[2] = {-1, -1};
    int target_pipe[2] = {-1, -1};
    if (::pipe(image_pipe) != 0) {
        throw std::runtime_error("image pipe creation failed");
    }
    if (::pipe(target_pipe) != 0) {
        ::close(image_pipe[0]);
        ::close(image_pipe[1]);
        throw std::runtime_error("target pipe creation failed");
    }
    ::close(image_pipe[1]);
    ::close(target_pipe[1]);
    return iso_flash::WriteJob(
        id, iso_flash::UniqueFd(image_pipe[0]),
        iso_flash::UniqueFd(target_pipe[0]));
}

std::unique_ptr<iso_flash::WriteWorker> make_worker(std::uint64_t id) {
    return std::make_unique<iso_flash::WriteWorker>(
        make_job(id), 8,
        [](iso_flash::WriteJob&) { return 0; },
        [](iso_flash::WriteJob&,
           const iso_flash::WriteWorker::ProgressCallback& progress,
           const iso_flash::WriteWorker::CancelCallback&) {
            if (!progress(8)) {
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Failed, 0, EIO};
            }
            return iso_flash::WriteOperationResult{
                iso_flash::WriteIoStatus::Completed, 8, 0};
        });
}

}  // namespace

int main() {
    iso_flash::WriteJobRegistry registry;
    if (!registry.add(make_worker(41)) || registry.size() != 1) {
        std::cerr << "Registry rejected a valid job\n";
        return 1;
    }
    if (registry.add(make_worker(41)) || registry.size() != 1) {
        std::cerr << "Registry accepted a duplicate job ID\n";
        return 1;
    }
    if (registry.snapshot(999).has_value() || !registry.snapshot(41).has_value()) {
        std::cerr << "Registry lookup returned an incorrect result\n";
        return 1;
    }

    std::size_t reaped = 0;
    for (int attempt = 0; attempt < 100 && reaped == 0; ++attempt) {
        reaped = registry.reap_terminal();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (reaped != 1 || registry.size() != 0) {
        std::cerr << "Registry did not reap a completed job\\n";
        return 1;
    }
    if (registry.snapshot(41).has_value()) {
        std::cerr << "Reaped job remained visible\\n";
        return 1;
    }

    std::promise<void> operation_entered;
    std::promise<void> release_operation;
    const auto release_signal = release_operation.get_future().share();
    auto cancellable = std::make_unique<iso_flash::WriteWorker>(
        make_job(42), 8,
        [](iso_flash::WriteJob&) { return 0; },
        [&operation_entered, release_signal](
            iso_flash::WriteJob&,
            const iso_flash::WriteWorker::ProgressCallback&,
            const iso_flash::WriteWorker::CancelCallback& cancelled) {
            operation_entered.set_value();
            release_signal.wait();
            if (cancelled()) {
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Cancelled, 0, 0};
            }
            return iso_flash::WriteOperationResult{
                iso_flash::WriteIoStatus::Completed, 8, 0};
        });
    if (!registry.add(std::move(cancellable))) {
        std::cerr << "Registry rejected second unique job\\n";
        return 1;
    }
    operation_entered.get_future().wait();
    if (!registry.cancel(42) || registry.cancel(999)) {
        std::cerr << "Registry cancellation lookup failed\\n";
        return 1;
    }
    release_operation.set_value();

    registry.shutdown();
    if (registry.size() != 0 || registry.add(make_worker(44))) {
        std::cerr << "Registry did not close admission during shutdown\n";
        return 1;
    }

    return 0;
}
