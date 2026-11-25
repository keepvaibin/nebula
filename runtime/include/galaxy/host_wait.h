// SPDX-License-Identifier: GPL-3.0-only
// The SDL host's waits (P-281, docs/plan/native/05-NATIVE-LANE-G.md G2.3.6), under host_wait_tests.cpp: the event pump's
// block that an event or a queued request ends at once, and the display clock's sleep that only an interruption sends
// back to sleep.
#pragma once

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace galaxy {

// A bounded block of one thread that any other thread ends at once (`wake`). A wake that comes while the thread is not
// blocked is kept: the next block returns at once, so nothing that came between the thread's last look and its block
// waits for the bound.
class WakeWait {
public:
    void wake() {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            woken_ = true;
        }
        ready_.notify_one();
    }
    // Forgets a wake kept from before: the caller is about to look at everything a wake would have told it of.
    void clear() {
        const std::lock_guard<std::mutex> lock(mutex_);
        woken_ = false;
    }
    // Blocks until woken or `bound` has passed; true when woken. The wake is taken.
    bool wait_for(std::chrono::nanoseconds bound) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool woken = ready_.wait_for(lock, bound, [this] { return woken_; });
        woken_ = false;
        return woken;
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    bool woken_ = false;
};

// A sleep to an absolute time in clock_nanosleep's form (`sleep()` gives 0 when it slept to the time, else the error),
// slept again only when a signal broke it off (EINTR). True when it slept; false on any other error, which sleeping again
// would only repeat (an EINVAL every time: the thread would spin for good): the caller hands the frame to the host's
// clock.
template <class Sleep>
[[nodiscard]] bool sleep_through_interruptions(Sleep&& sleep) {
    for (;;) {
        const int error = sleep();
        if (error == 0) return true;
        if (error != EINTR) return false;
    }
}

}  // namespace galaxy
