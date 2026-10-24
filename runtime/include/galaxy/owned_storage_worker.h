#pragma once

#include <atomic>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace galaxy::host {

// Exactly one owned physical transaction until its completion is consumed.
// Never executes guest code. A wake callback may only publish a notification;
// it must not reenter this worker. Clearing it waits out any in-flight wake.
template <typename Work>
class OwnedStorageWorker {
public:
    using Execute = void (*)(Work&);
    using Wake = void (*)(void*) noexcept;

    explicit OwnedStorageWorker(Execute execute)
        : execute_(execute), thread_([this] { run(); }) {}
    ~OwnedStorageWorker() { shutdown(); }
    OwnedStorageWorker(const OwnedStorageWorker&) = delete;
    OwnedStorageWorker& operator=(const OwnedStorageWorker&) = delete;

    bool submit(std::unique_ptr<Work>& work) {
        std::lock_guard lock(mutex_);
        if (stopping_ || occupied_ || !work) return false;
        occupied_ = true;
        work_ = std::move(work);
        condition_.notify_one();
        return true;
    }
    void shutdown() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            wake_ = nullptr;
            wake_user_ = nullptr;
        }
        condition_.notify_one();
        if (thread_.joinable()) thread_.join();
    }

private:
    void run() noexcept {
        for (;;) {
            std::unique_ptr<Work> work;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [&] { return stopping_ || work_ != nullptr; });
                if (!work_) return;
                work = std::move(work_);
            }
            std::exception_ptr fatal;
            try { execute_(*work); } catch (...) { fatal = std::current_exception(); }
            {
                std::lock_guard lock(mutex_);
                completed_ = std::move(work);
                fatal_ = std::move(fatal);
                ready_.store(true, std::memory_order_release);
                if (wake_) wake_(wake_user_);
            }
        }
    }

    Execute execute_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::unique_ptr<Work> work_;
    std::unique_ptr<Work> completed_;
    std::exception_ptr fatal_;
    std::atomic_bool ready_{};
    bool stopping_{};
    bool occupied_{};
    Wake wake_{};
    void* wake_user_{};
    // Declared last: every shared field exists before the worker starts.
    std::thread thread_;
};

}  // namespace galaxy::host
