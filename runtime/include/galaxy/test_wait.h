#pragma once

// A unit test's wait for another thread: it waits for the event or condition itself, looked at every few milliseconds,
// and gives up only after kTestWaitDeadline. The deadline says "the event never came"; no test passes or fails by
// how long an event took (a machine with every processor busy runs a lowered thread seconds late: N-1073).

#include <chrono>
#include <thread>

namespace galaxy {

inline constexpr std::chrono::seconds kTestWaitDeadline{60};

// True once done() is true; false when it still is not after `deadline`. done() is looked at once more at the deadline.
template <class Done>
[[nodiscard]] bool test_wait(Done&& done, std::chrono::milliseconds step = std::chrono::milliseconds(5),
                             std::chrono::milliseconds deadline = kTestWaitDeadline) {
    const auto until = std::chrono::steady_clock::now() + deadline;
    while (!done()) {
        if (std::chrono::steady_clock::now() >= until) {
            return done();
        }
        std::this_thread::sleep_for(step);
    }
    return true;
}

}  // namespace galaxy
