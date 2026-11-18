// SPDX-License-Identifier: GPL-3.0-only
// galaxy/bounded_call.h: a call that waits on a lock another thread holds returns within its time (P-067: on Linux,
// macOS and Android the fatal-error path wrote achievements and stats inline and waited for ever on such a lock).
#include "galaxy/bounded_call.h"

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
    passed &= expect(galaxy::call_bounded(g_quick, 5000u) && g_quick_calls == 1, "a call that ends is waited for");

    g_held.lock();  // as the failed thread holds the progress lock
    const auto start = std::chrono::steady_clock::now();
    const bool ended = galaxy::call_bounded(g_blocked, 300u);
    const auto took = std::chrono::steady_clock::now() - start;
    passed &= expect(!ended, "a call blocked on a held lock does not end");
    passed &= expect(took >= std::chrono::milliseconds(250) && took < std::chrono::milliseconds(3000),
                     "the wait ends at its time, not with the call");
    g_held.unlock();  // the blocked call ends now, on its own thread
    for (int i = 0; i < 200 && !g_blocked.done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    passed &= expect(g_blocked.done.load(), "the call itself goes on and ends once the lock is free");

    if (!passed) return 1;
    std::printf("bounded call tests passed\n");
    return 0;
}
