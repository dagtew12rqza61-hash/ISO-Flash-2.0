#include "image_validation_limiter.hpp"

#include <array>
#include <atomic>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

int main() {
    iso_flash::ImageValidationLimiter limiter(2);
    auto first = limiter.try_acquire();
    auto second = limiter.try_acquire();
    auto third = limiter.try_acquire();
    if (!first || !second || third || limiter.active() != 2) {
        std::cerr << "Limiter did not enforce its two-task capacity\n";
        return 1;
    }

    first.reset();
    auto replacement = limiter.try_acquire();
    if (!replacement || limiter.active() != 2) {
        std::cerr << "Released capacity was not made available\n";
        return 1;
    }

    iso_flash::ImageValidationLimiter::Lease moved(std::move(replacement));
    if (replacement || !moved || limiter.active() != 2) {
        std::cerr << "Moving a lease changed the active count\n";
        return 1;
    }
    moved = {};
    if (limiter.active() != 1) {
        std::cerr << "Move assignment did not release exactly one lease\n";
        return 1;
    }
    second.reset();
    if (limiter.active() != 0) {
        std::cerr << "Lease reset did not return the limiter to zero\n";
        return 1;
    }

    iso_flash::ImageValidationLimiter concurrent(2);
    std::atomic<unsigned int> admitted{0};
    std::atomic<unsigned int> peak{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    threads.reserve(24);
    for (unsigned int i = 0; i < 24; ++i) {
        threads.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            auto lease = concurrent.try_acquire();
            if (lease) {
                const auto current = admitted.fetch_add(1, std::memory_order_acq_rel) + 1;
                auto observed = peak.load(std::memory_order_acquire);
                while (observed < current &&
                       !peak.compare_exchange_weak(
                           observed, current, std::memory_order_acq_rel,
                           std::memory_order_acquire)) {
                }
                std::this_thread::yield();
                admitted.fetch_sub(1, std::memory_order_acq_rel);
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) {
        thread.join();
    }
    if (peak.load(std::memory_order_acquire) > 2 ||
        concurrent.active() != 0) {
        std::cerr << "Concurrent acquisitions exceeded capacity or leaked a lease\n";
        return 1;
    }

    iso_flash::ImageValidationLimiter zero_capacity(0);
    if (zero_capacity.try_acquire() || zero_capacity.active() != 0) {
        std::cerr << "Zero-capacity limiter admitted a task\n";
        return 1;
    }

    return 0;
}
