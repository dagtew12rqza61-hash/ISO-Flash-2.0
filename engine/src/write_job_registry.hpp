#pragma once

#include "write_worker.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace iso_flash {

class WriteJobRegistry {
public:
    WriteJobRegistry() = default;
    WriteJobRegistry(const WriteJobRegistry&) = delete;
    WriteJobRegistry& operator=(const WriteJobRegistry&) = delete;

    ~WriteJobRegistry() {
        shutdown();
    }

    bool add(std::unique_ptr<WriteWorker> worker) {
        if (!worker || worker->id() == 0) {
            return false;
        }
        const std::uint64_t job_id = worker->id();
        const std::lock_guard<std::mutex> lock(mutex_);
        if (shutting_down_ || jobs_.find(job_id) != jobs_.end()) {
            return false;
        }
        auto inserted = jobs_.emplace(job_id, std::move(worker));
        if (!inserted.second) {
            return false;
        }
        if (!inserted.first->second->start()) {
            jobs_.erase(inserted.first);
            return false;
        }
        return true;
    }

    std::optional<WriteJobSnapshot> snapshot(std::uint64_t job_id) const {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = jobs_.find(job_id);
        if (it == jobs_.end()) {
            return std::nullopt;
        }
        return it->second->snapshot();
    }

    bool cancel(std::uint64_t job_id) {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = jobs_.find(job_id);
        return it != jobs_.end() && it->second->cancel();
    }

    std::size_t size() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return jobs_.size();
    }

    std::size_t reap_terminal() {
        std::vector<std::unique_ptr<WriteWorker>> retired;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (auto it = jobs_.begin(); it != jobs_.end();) {
                if (is_terminal(it->second->snapshot().state)) {
                    retired.push_back(std::move(it->second));
                    it = jobs_.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto& worker : retired) {
            worker->join();
        }
        return retired.size();
    }

    void shutdown() noexcept {
        std::vector<std::unique_ptr<WriteWorker>> retired;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (shutting_down_ && jobs_.empty()) {
                return;
            }
            shutting_down_ = true;
            retired.reserve(jobs_.size());
            for (auto& entry : jobs_) {
                retired.push_back(std::move(entry.second));
            }
            jobs_.clear();
        }
        for (auto& worker : retired) {
            worker->cancel();
        }
        for (auto& worker : retired) {
            worker->join();
        }
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::unique_ptr<WriteWorker>> jobs_;
    bool shutting_down_ = false;
};

}  // namespace iso_flash
