// galaxy/host_wait.h (P-281): the event pump's block ends at a wake from another thread, never loses a wake that came
// before it, and runs to its bound with none; the display clock's sleep is tried again on EINTR only.
#include "galaxy/host_wait.h"

#include <atomic>
#include <cerrno>
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
    using Clock = std::chrono::steady_clock;
    bool passed = true;

    // No wake: the block runs to its bound (8 ms, the pump's), and says it was not woken.
    {
        galaxy::WakeWait wait;
        const auto began = Clock::now();
        passed &= expect(!wait.wait_for(8ms), "a block with no wake is not woken");
        passed &= expect(Clock::now() - began >= 8ms, "and lasts its bound");
    }

    // A wake from another thread while blocked: the block ends long before its bound (the old pump slept it all out).
    {
        galaxy::WakeWait wait;
        std::atomic<bool> blocking{false};
        std::thread waker([&] {
            while (!blocking.load()) std::this_thread::yield();
            std::this_thread::sleep_for(20ms);
            wait.wake();
        });
        blocking.store(true);
        const auto began = Clock::now();
        const bool woken = wait.wait_for(10s);
        const auto took = Clock::now() - began;
        waker.join();
        passed &= expect(woken, "a wake ends the block");
        passed &= expect(took < 5s, "at once, not at the bound");
    }

    // A wake before the block (an event pushed between the pump's look and its block): kept, the block returns at once.
    {
        galaxy::WakeWait wait;
        wait.wake();
        const auto began = Clock::now();
        passed &= expect(wait.wait_for(10s), "a wake before the block is kept");
        passed &= expect(Clock::now() - began < 5s, "and the block does not wait");
        // Taken by that block: the next one waits again.
        passed &= expect(!wait.wait_for(1ms), "a wake is taken once");
    }

    // A wake cleared before the pump looks (it is about to see what the wake was for): the block waits its bound.
    {
        galaxy::WakeWait wait;
        wait.wake();
        wait.clear();
        passed &= expect(!wait.wait_for(1ms), "a cleared wake does not end the next block");
    }

    // The display clock's sleep: slept again after interruptions, until it sleeps.
    {
        int calls = 0;
        const bool slept = galaxy::sleep_through_interruptions([&] { return ++calls < 3 ? EINTR : 0; });
        passed &= expect(slept && calls == 3, "EINTR sleeps again until the sleep ends");
    }
    // Any other error ends it at once (the old loop slept again on every error: an EINVAL spun its thread for good).
    {
        int calls = 0;
        const bool slept = galaxy::sleep_through_interruptions([&] { ++calls; return EINVAL; });
        passed &= expect(!slept && calls == 1, "EINVAL is not slept again");
    }
    {
        int calls = 0;
        const bool slept = galaxy::sleep_through_interruptions([&] { return ++calls == 1 ? EINTR : EFAULT; });
        passed &= expect(!slept && calls == 2, "an interruption then another error ends at the error");
    }

    if (!passed) return 1;
    std::printf("host wait tests passed\n");
    return 0;
}
