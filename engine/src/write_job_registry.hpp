#pragma once

#include "write_worker.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace iso_flash {

struct WriteJobInfo {
    std::uint64_t id = 0;
    std::string owner;
    WriteJobSnapshot snapshot;
};

class WriteJobRegistry {
public:
    WriteJobRegistry() = default;
    WriteJobRegistry(const WriteJobRegistry&) = delete;
    WriteJobRegistry& operator=(const WriteJobRegistry&) = delete;

    ~WriteJobRegistry() {
        shutdown();
    }

    bool add(std::string owner, std::unique_ptr<WriteWorker> worker) {
        if (owner.empty() || owner.front() != ':' || !worker ||
            worker->id() == 0) {
            return false;
        }
        const std::uint64_t job_id = worker->id();
        const std::lock_guard<std::mutex> lock(mutex_);
        if (shutting_down_ || jobs_.find(job_id) != jobs_.end()) {
            return false;
        }
        Entry entry{std::move(owner), std::move(worker)};
        auto inserted = jobs_.emplace(job_id, std::move(entry));
        if (!inserted.second) {
            return false;
        }
        if (!inserted.first->second.worker->start()) {
            jobs_.erase(inserted.first);
            return false;
        }
        return true;
    }

    std::optional<WriteJobSnapshot> snapshot(
        const std::string& owner, std::uint64_t job_id) const {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = jobs_.find(job_id);
        if (it == jobs_.end() || it->second.owner != owner) {
            return std::nullopt;
        }
        return it->second.worker->snapshot();
    }

    std::vector<WriteJobInfo> snapshots() const {
        std::vector<WriteJobInfo> result;
        const std::lock_guard<std::mutex> lock(mutex_);
        result.reserve(jobs_.size());
        for (const auto& entry : jobs_) {
            result.push_back(
                {entry.first, entry.second.owner,
                 entry.second.worker->snapshot()});
        }
        return result;
    }

    bool cancel(const std::string& owner, std::uint64_t job_id) {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = jobs_.find(job_id);
        return it != jobs_.end() && it->second.owner == owner &&
               it->second.worker->cancel();
    }

    std::size_t cancel_owner(const std::string& owner) {
        std::size_t cancelled = 0;
        const std::lock_guard<std::mutex> lock(mutex_);
        for (auto& entry : jobs_) {
            if (entry.second.owner == owner &&
                entry.second.worker->cancel()) {
                ++cancelled;
            }
        }
        return cancelled;
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
                if (is_terminal(it->second.worker->snapshot().state)) {
                    retired.push_back(std::move(it->second.worker));
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
        std::unordered_map<std::uint64_t, Entry> retired;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            shutting_down_ = true;
            retired.swap(jobs_);
        }
        for (auto& entry : retired) {
            entry.second.worker->cancel();
        }
        for (auto& entry : retired) {
            entry.second.worker->join();
        }
    }

private:
    struct Entry {
        std::string owner;
        std::unique_ptr<WriteWorker> worker;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, Entry> jobs_;
    bool shutting_down_ = false;
};

}  // namespace iso_flash
