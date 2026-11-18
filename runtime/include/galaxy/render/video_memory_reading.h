// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The video memory a renderer reports (its use and its budget), as every renderer reads it (P-011, P-022): the driver
// is asked at most every kEveryMs, since the use moves with every allocation and the render core asks on every walk
// (enforce_video_memory, generated pictures included); a budget the OS says it moved is read at once.

#include <cstdint>

namespace galaxy::render {

class VideoMemoryReading {
public:
    static constexpr std::int64_t kEveryMs = 100;

    // `ask(usage, budget)` asks the driver, true when it answered. The figures at `now_ms`: false when none are known
    // (the driver never answered with a budget). An answer with no budget is no answer; a failed ask keeps the figures
    // read before, and the next call asks again.
    template <class Ask>
    bool read(std::int64_t now_ms, bool budget_moved, Ask&& ask, std::uint64_t& usage, std::uint64_t& budget) {
        if (budget_moved || budget_ == 0u || now_ms - read_ms_ >= kEveryMs) {
            std::uint64_t asked_usage = 0u;
            std::uint64_t asked_budget = 0u;
            if (ask(asked_usage, asked_budget) && asked_budget != 0u) {
                read_ms_ = now_ms;
                usage_ = asked_usage;
                budget_ = asked_budget;
            }
        }
        usage = usage_;
        budget = budget_;
        return budget_ != 0u;
    }
    // The budget last read (0: none), for a line that says how it moved.
    [[nodiscard]] std::uint64_t budget() const noexcept { return budget_; }
    // A new device or adapter: the next read asks.
    void forget() noexcept { *this = VideoMemoryReading{}; }

private:
    std::int64_t read_ms_ = 0;
    std::uint64_t usage_ = 0u;
    std::uint64_t budget_ = 0u;
};

}  // namespace galaxy::render
