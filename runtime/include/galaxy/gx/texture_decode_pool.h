#pragma once

// TextureDecodePool: a small fixed worker pool for first-use texture decodes.
//
// A job reads guest bytes that the frame's sealed memory snapshot keeps unchanged until the frame is submitted,
// and a palette copy it owns. The render thread records the staging-to-texture copy at first use (keeping upload
// order on the command list); only the fill of the staging bytes runs here. wait_all() must return before the
// command list that reads those bytes executes; the waiting thread runs queued jobs itself.
// Free of D3D12 and Windows, so its join and exception contract is unit-tested directly.

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace galaxy::gx {

class TextureDecodePool {
public:
    explicit TextureDecodePool(unsigned worker_count, void (*thread_start)() = nullptr) {
        workers_.reserve(worker_count);
        for (unsigned i = 0; i < worker_count; ++i) {
            workers_.emplace_back([this, thread_start] {
                if (thread_start != nullptr) thread_start();
                worker_main();
            });
        }
    }

    ~TextureDecodePool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        work_cv_.notify_all();
        for (std::thread& worker : workers_) {
            worker.join();
        }
        // Never abandon a submitted job: its staging memory is owned by a frame.
        run_queued_on_caller();
    }

    TextureDecodePool(const TextureDecodePool&) = delete;
    TextureDecodePool& operator=(const TextureDecodePool&) = delete;

    void submit(std::function<void()> job) {
        if (workers_.empty()) {
            run_one(job);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(job));
            ++outstanding_;
        }
        work_cv_.notify_one();
    }

    // Blocks until every submitted job has finished, running queued ones itself, then rethrows the first job
    // exception (once).
    void wait_all() {
        run_queued_on_caller();
        std::unique_lock<std::mutex> lock(mutex_);
        done_cv_.wait(lock, [this] { return outstanding_ == 0u; });
        if (error_) {
            std::exception_ptr error = std::exchange(error_, nullptr);
            lock.unlock();
            std::rethrow_exception(error);
        }
    }

    // The same join for unwinding paths: returns false if a job failed (the error is consumed).
    bool wait_all_noexcept() noexcept {
        try {
            wait_all();
            return true;
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] std::size_t outstanding() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return outstanding_;
    }
private:
    void run_one(std::function<void()>& job) {
        try {
            job();
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!error_) {
                error_ = std::current_exception();
            }
        }
    }

    void finish_one() {
        bool idle = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            idle = --outstanding_ == 0u;
        }
        if (idle) {
            done_cv_.notify_all();
        }
    }

    void run_queued_on_caller() {
        for (;;) {
            std::function<void()> job;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queue_.empty()) {
                    return;
                }
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            run_one(job);
            finish_one();
        }
    }

    void worker_main() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                work_cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) {
                    return;  // stopping and drained
                }
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            run_one(job);
            finish_one();
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable work_cv_;
    std::condition_variable done_cv_;
    std::deque<std::function<void()>> queue_;
    std::size_t outstanding_ = 0;
    std::exception_ptr error_;
    bool stopping_ = false;
    std::vector<std::thread> workers_;
};

}  // namespace galaxy::gx
