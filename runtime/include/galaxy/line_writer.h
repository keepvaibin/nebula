// SPDX-License-Identifier: GPL-3.0-only
// One thread that writes what every other thread hands it, so no thread but it waits on an output: a log file on a
// slow or busy disk, a pipe its reader stopped draining, a console. Text keeps the order it was handed in, across
// every output it names (stdout and stderr may be one file).
//
// hand() copies the text into a queue under a short lock and wakes the writer; it never writes. flush() waits, at most
// its bound, until the writer has written and flushed all that was handed before it; it never writes either, so a
// stuck output holds only the writer's thread. An end of the process (normal or not) calls it before it goes. A fault
// handler that may only make async-signal-safe calls cannot flush; what was handed in the last moments before such a
// fault may be lost.
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
    // (fflush). Both are called on the writer's thread only.
    using Write = void (*)(void* user, unsigned output, const char* data, std::size_t size);
    using FlushAll = void (*)(void* user);

    LineWriter(Write write, FlushAll flush_all, void* user) : write_(write), flush_all_(flush_all), user_(user) {}
    LineWriter(const LineWriter&) = delete;
    LineWriter& operator=(const LineWriter&) = delete;
    // Joins the writer, so the output must move again first (an owner that may end while it is stuck keeps the
    // writer for the life of the process).
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
        writer_ = thread_.get_id();
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
            ++handed_;
        }
        wake_.notify_one();
        return true;
    }

    // True once everything handed before this call is written and flushed. False when `bound` passed first: the
    // writer is stuck in a write that does not end, or a thread stopped holding the queue's lock (a crash). Called on
    // the writer's own thread (a fault in a write) it does not wait for itself.
    bool flush(std::chrono::milliseconds bound) {
        const auto until = std::chrono::steady_clock::now() + bound;
        std::unique_lock<std::mutex> lock(mutex_, std::defer_lock);
        while (!lock.try_lock()) {
            if (std::chrono::steady_clock::now() >= until) {
                return false;
            }
            std::this_thread::yield();
        }
        const std::uint64_t want = handed_;
        if (written_ >= want) {
            return true;
        }
        if (std::this_thread::get_id() == writer_) {
            return false;
        }
        return done_.wait_until(lock, until, [&] { return written_ >= want; });
    }

    // No more text is taken (hand returns false); waits, at most `bound`, for what was taken to be written. The
    // thread ends once it is.
    bool stop(std::chrono::milliseconds bound = std::chrono::milliseconds(2000)) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
            if (!running_) {
                return written_ >= handed_;
            }
            running_ = false;
        }
        wake_.notify_one();
        return flush(bound);
    }

    // stop(), then the thread is joined once it has written all it took, or let go when `bound` passed first (a stuck
    // output): an owner that keeps the writer for the life of the process calls it as the process ends, so no thread
    // is left unjoined (a sanitizer's thread leak). On one thread only, and not the writer's.
    bool finish(std::chrono::milliseconds bound = std::chrono::milliseconds(2000)) {
        const bool done = stop(bound);
        if (thread_.joinable()) {
            if (done) {
                thread_.join();  // nothing left: the writer returns as soon as it sees stopping_
            } else {
                thread_.detach();
            }
        }
        return done;
    }

private:
    void run() {
        for (;;) {
            std::uint64_t taken = 0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return !pending_.empty() || stopping_; });
                if (pending_.empty()) {
                    return;  // stopping, all written
                }
                batch_.swap(pending_);
                taken = handed_;
            }
            for (std::size_t at = 0; at + 5u <= batch_.size();) {
                std::uint32_t length = 0;
                std::memcpy(&length, batch_.data() + at + 1u, sizeof(length));
                write_(user_, static_cast<unsigned char>(batch_[at]), batch_.data() + at + 5u, length);
                at += 5u + length;
            }
            batch_.clear();
            flush_all_(user_);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                written_ = taken;
            }
            done_.notify_all();
        }
    }

    Write write_;
    FlushAll flush_all_;
    void* user_;
    std::mutex mutex_;              // pending_, running_, stopping_, handed_, written_
    std::condition_variable wake_;  // the writer: text handed, or stopping
    std::condition_variable done_;  // flush: written_ moved
    std::string pending_;           // [output byte][length, 4 bytes][text] ...
    bool running_ = false;
    bool stopping_ = false;
    std::uint64_t handed_ = 0;      // texts handed so far
    std::uint64_t written_ = 0;     // of those, written and flushed
    std::string batch_;             // the writer's thread only
    std::thread thread_;            // start, finish and the destructor only
    std::thread::id writer_;        // thread_'s, set under mutex_ (flush reads it while finish may join)
};

}  // namespace galaxy
