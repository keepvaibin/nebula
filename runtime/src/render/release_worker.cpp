// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/release_worker.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace galaxy::render {

namespace {

class ReleaseWorker {
public:
    ~ReleaseWorker() {
        {
            const std::lock_guard guard(lock_);
            stop_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void push(std::function<void()> release) {
        {
            const std::lock_guard guard(lock_);
            if (!thread_.joinable()) {
                thread_ = std::thread([this] { run(); });
            }
            queue_.push_back(std::move(release));
        }
        wake_.notify_all();
    }

    void wait() {
        if (thread_id_.load(std::memory_order_acquire) == std::this_thread::get_id()) {
            return;
        }
        std::unique_lock lock(lock_);
        done_.wait(lock, [this] { return queue_.empty() && !busy_; });
    }

private:
    void run() {
        thread_id_.store(std::this_thread::get_id(), std::memory_order_release);
        std::unique_lock lock(lock_);
        for (;;) {
            wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty()) {
                return;  // stopping, everything released
            }
            std::function<void()> release = std::move(queue_.front());
            queue_.pop_front();
            busy_ = true;
            lock.unlock();
            release();
            release = nullptr;
            lock.lock();
            busy_ = false;
            if (queue_.empty()) {
                done_.notify_all();
            }
        }
    }

    std::mutex lock_;
    std::condition_variable wake_;
    std::condition_variable done_;
    std::deque<std::function<void()>> queue_;
    bool busy_ = false;
    bool stop_ = false;
    std::thread thread_;
    std::atomic<std::thread::id> thread_id_{};
};

ReleaseWorker& worker() {
    static ReleaseWorker instance;
    return instance;
}

}  // namespace

void release_later(std::function<void()> release) {
    worker().push(std::move(release));
}

void release_wait() noexcept {
    worker().wait();
}

}  // namespace galaxy::render
