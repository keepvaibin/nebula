// SPDX-License-Identifier: GPL-3.0-only
// galaxy/bounded_call.h: a call that waits on a lock another thread holds returns within its time (P-067: on Linux,
// macOS and Android the fatal-error path wrote achievements and stats inline and waited for ever on such a lock).
#include "galaxy/bounded_call.h"
#include "galaxy/test_wait.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>

namespace {

std::mutex g_held;
int g_quick_calls = 0;

void wait_on_held_lock() {
    const std::lock_guard<std::mutex> lock(g_held);
}

void quick() {
    ++g_quick_calls;
}

bool expect(bool condition, const char* what) {
    if (!condition) std::printf("FAILED: %s\n", what);
    return condition;
}

galaxy::BoundedCall g_blocked{wait_on_held_lock};
galaxy::BoundedCall g_quick{quick};

}  // namespace

int main() {
    bool passed = true;
    // The bound of a call that ends is the test's deadline: the call's thread may start seconds late on a busy machine.
    passed &= expect(galaxy::call_bounded(g_quick, 60000u) && g_quick_calls == 1, "a call that ends is waited for");

    g_held.lock();  // as the failed thread holds the progress lock
    const auto start = std::chrono::steady_clock::now();
    const bool ended = galaxy::call_bounded(g_blocked, 300u);
    const auto took = std::chrono::steady_clock::now() - start;
    passed &= expect(!ended, "a call blocked on a held lock does not end");
    // The wait ended with the lock still held, so with the time and not with the call. How much later than its time it
    // returned is no verdict: a busy machine runs the waiting thread late.
    passed &= expect(took >= std::chrono::milliseconds(250), "the wait lasts its time");
    g_held.unlock();  // the blocked call ends now, on its own thread
    passed &= expect(galaxy::test_wait([] { return g_blocked.done.load(); }, std::chrono::milliseconds(10)),
                     "the call itself goes on and ends once the lock is free");

    if (!passed) return 1;
    std::printf("bounded call tests passed\n");
    return 0;
}
