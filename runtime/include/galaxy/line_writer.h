// SPDX-License-Identifier: GPL-3.0-only
// One thread that writes what every other thread hands it, so no thread but it waits on an output: a log file on a
// slow or busy disk, a pipe its reader stopped draining, a console. Text keeps the order it was handed in, across
// every output it names (stdout and stderr may be one file).
//
// hand() copies the text into a queue under a short lock and wakes the writer; it never writes. flush() writes all
// that was handed before it on the calling thread, bounded: an end of the process (normal or not) calls it before it
// goes. A fault handler that may only make async-signal-safe calls cannot flush; what was handed in the last moments
// before such a fault may be lost.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

namespace galaxy {

class LineWriter {
public:
    // Writes `size` bytes to output `output` (a small number the owner chose); flush_all: every output's data is out
    // (fflush). Both are called on the writer's thread, or on a thread in flush().
    using Write = void (*)(void* user, unsigned output, const char* data, std::size_t size);
    using FlushAll = void (*)(void* user);

    LineWriter(Write write, FlushAll flush_all, void* user) : write_(write), flush_all_(flush_all), user_(user) {}
    LineWriter(const LineWriter&) = delete;
    LineWriter& operator=(const LineWriter&) = delete;
    ~LineWriter() {
        stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // The writer's thread starts. Once.
    void start() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_ || stopping_) {
            return;
        }
        running_ = true;
        thread_ = std::thread([this] { run(); });
    }

    // Queues the text; false when the writer is not running (not started or stopped): the caller writes it itself.
    bool hand(unsigned output, const char* data, std::size_t size) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                return false;
            }
            const auto length = static_cast<std::uint32_t>(size);
            char head[5] = {static_cast<char>(output)};
            std::memcpy(head + 1, &length, sizeof(length));
            pending_.append(head, sizeof(head));
            pending_.append(data, size);
        }
        wake_.notify_one();
        return true;
    }

    // Everything handed before this call is written and flushed, on this thread. False when `bound` passed first (the
    // writer is stuck in a write that does not end, or a thread stopped holding the queue).
    bool flush(std::chrono::milliseconds bound) {
        std::unique_lock<std::timed_mutex> writing(writing_, std::defer_lock);
        if (!writing.try_lock_for(bound)) {
            return false;
        }
        return write_pending(&bound);
    }

    // No more text is taken (hand returns false), and what was taken is written. The thread ends with it.
    void stop(std::chrono::milliseconds bound = std::chrono::milliseconds(2000)) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!running_) {
                stopping_ = true;
                return;
            }
            running_ = false;
            stopping_ = true;
        }
        wake_.notify_one();
        (void)flush(bound);
    }

private:
    void run() {
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return !pending_.empty() || stopping_; });
                if (pending_.empty() && stopping_) {
                    return;
                }
            }
            std::lock_guard<std::timed_mutex> writing(writing_);
            (void)write_pending(nullptr);
        }
    }

    // Under writing_: takes the queue and writes it. `bound` (a flush) waits that long at most for the queue's lock.
    bool write_pending(const std::chrono::milliseconds* bound) {
        {
            std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
            if (bound == nullptr) {
                lock.lock();
            } else {
                const auto until = std::chrono::steady_clock::now() + *bound;
                while (!lock.try_lock()) {
                    if (std::chrono::steady_clock::now() >= until) {
                        return false;
                    }
                    std::this_thread::yield();
                }
            }
            batch_.swap(pending_);
        }
        for (std::size_t at = 0; at + 5u <= batch_.size();) {
            std::uint32_t length = 0;
            std::memcpy(&length, batch_.data() + at + 1u, sizeof(length));
            write_(user_, static_cast<unsigned char>(batch_[at]), batch_.data() + at + 5u, length);
            at += 5u + length;
        }
        batch_.clear();
        flush_all_(user_);
        return true;
    }

    Write write_;
    FlushAll flush_all_;
    void* user_;
    std::mutex mutex_;              // pending_, running_, stopping_
    std::condition_variable wake_;
    std::string pending_;           // [output byte][length, 4 bytes][text] ...
    bool running_ = false;
    bool stopping_ = false;
    std::timed_mutex writing_;      // one writer at a time, so batches go out in the order they were taken
    std::string batch_;             // under writing_
    std::thread thread_;
};

}  // namespace galaxy
