#include "write_job_registry.hpp"

#include <cstdint>
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

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (registry.reap_terminal() != 1 || registry.size() != 0) {
        std::cerr << "Registry did not reap a completed job\\n";
        return 1;
    }
    if (registry.snapshot(41).has_value()) {
        std::cerr << "Reaped job remained visible\\n";
        return 1;
    }

    if (!registry.add(std::make_unique<iso_flash::WriteWorker>(
            make_job(42), 8,
            [](iso_flash::WriteJob&) { return 0; },
            [](iso_flash::WriteJob&,
               const iso_flash::WriteWorker::ProgressCallback&,
               const iso_flash::WriteWorker::CancelCallback&) {
                return iso_flash::WriteOperationResult{
                    iso_flash::WriteIoStatus::Cancelled, 0, 0};
            }))) {
        std::cerr << "Registry rejected second unique job\n";
        return 1;
    }
    if (!registry.cancel(42) || registry.cancel(999)) {
        std::cerr << "Registry cancellation lookup failed\n";
        return 1;
    }

    auto worker = make_worker(43);
    if (!registry.add(std::move(worker))) {
        std::cerr << "Registry rejected third job\n";
        return 1;
    }

    if (!registry.cancel(43)) {
        std::cerr << "Registry did not forward cancellation\n";
        return 1;
    }

    registry.shutdown();
    if (registry.size() != 0 || registry.add(make_worker(44))) {
        std::cerr << "Registry did not close admission during shutdown\n";
        return 1;
    }

    return 0;
}
