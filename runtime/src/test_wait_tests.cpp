// galaxy/test_wait.h: a wait ends on its condition, however late a thread makes it true, and gives up only at its deadline.
#include "galaxy/test_wait.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

bool expect(bool condition, const char* what) {
    if (!condition) std::printf("FAILED: %s\n", what);
    return condition;
}

}  // namespace

int main() {
    using namespace std::chrono_literals;
    bool passed = true;

    // A condition already true: no wait at all.
    passed &= expect(galaxy::test_wait([] { return true; }, 1ms, 1ms), "a true condition is true");

    // A thread that makes the condition true late: the wait is far longer than its own step and still ends true.
    std::atomic<bool> flag{false};
    std::thread late([&] {
        std::this_thread::sleep_for(150ms);
        flag.store(true);
    });
    passed &= expect(galaxy::test_wait([&] { return flag.load(); }, 1ms), "a late event is waited for, not given up on");
    late.join();

    // A condition that never comes: false, at the deadline given (not before it).
    const auto began = std::chrono::steady_clock::now();
    passed &= expect(!galaxy::test_wait([] { return false; }, 1ms, 100ms), "an event that never comes is reported");
    passed &= expect(std::chrono::steady_clock::now() - began >= 100ms, "and not before its deadline");

    // The condition that turns true only after the deadline has passed, seen at the last look (the look at the deadline).
    std::atomic<int> looks{0};
    passed &= expect(galaxy::test_wait([&] { return ++looks >= 3; }, 40ms, 10ms),
                     "the look at the deadline counts: true when the condition is true then");

    if (!passed) return 1;
    std::printf("test wait tests passed\n");
    return 0;
}
