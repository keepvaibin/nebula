// SPDX-License-Identifier: GPL-3.0-only
// Tests of the presentation times (present_slots.h) with simulated displays: interpolated pictures at refresh rates
// that are and are not multiples of 60, one and two vsyncs apart, and the game's own frames, under steady, varying and
// spiking GPU loads. The motion is even when the time from each picture's moment to when it was shown matches the
// picture before it's; it may change only where the pipeline depth does, and around a hitch.
#include "galaxy/render/present_slots.h"

#include "galaxy/game_step.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>

using namespace galaxy::render;

namespace {

int failures = 0;

// A fixed-seed generator: the same run every time.
struct Random {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    double next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<double>(state >> 11) / static_cast<double>(1ull << 53);
    }
};

enum class Load { Steady, Varying, Spikes };
const char* load_name(Load load) {
    return load == Load::Steady ? "steady" : load == Load::Varying ? "varying" : "spikes";
}

// The compositor latches a picture this long before the vsync it is shown at.
constexpr std::int64_t kLatchNs = 2'000'000;
// The CPU's part of each picture before the GPU starts it.
constexpr std::int64_t kCpuNs = 1'000'000;
// The past presentation times come back this many presents later.
constexpr std::size_t kFeedbackLag = 3u;
constexpr double kSeconds = 60.0;

struct Result {
    unsigned uneven = 0;          // after the first 5 s: pictures shown a different time after their moment than the
                                  // picture before, with the same depth
    unsigned uneven_hitch = 0;    // of them, those within a few pictures of a hitch
    unsigned changes_late = 0;    // depth changes in the last 30 s
    unsigned skips = 0;           // slots the renderer skipped for a lower depth
    std::int64_t depth_most = 0;
    std::int64_t latency_last = 0;  // the last picture's time from its moment to when it was shown
};

// Interpolated pictures `slot_vsyncs` vsyncs apart on an `hz` display. The render thread renders each at its slot's
// moment (skipping a slot when the presentation times ask), the CPU takes 1 ms and the GPU a share of the present
// period, one picture at a time. The compositor shows a picture at the first vsync at or after the one it asked for
// that it was latched for, and after the picture before it.
Result run(double hz, unsigned slot_vsyncs, Load load) {
    const std::int64_t vsync = std::llround(1e9 / hz);
    const std::int64_t step = vsync * slot_vsyncs;
    const std::int64_t grid = 1'000'000'000;  // a vblank
    auto vsync_at_or_after = [&](std::int64_t t) { return grid + (t - grid + vsync - 1) / vsync * vsync; };
    PresentSlots slots;
    Random random;
    struct Pending {
        std::int64_t requested, earliest, actual, margin;
    };
    std::deque<Pending> feedback;
    struct Shown {
        std::int64_t latency, depth;
        bool hitch;
    };
    std::vector<Shown> shown;
    std::int64_t gpu_free = 0;
    std::int64_t last_actual = 0;
    unsigned hitch_left = 0;
    Result result;
    const std::int64_t end = grid + static_cast<std::int64_t>(kSeconds * 1e9);
    unsigned i = 0;
    for (std::int64_t moment = grid; moment < end; moment += step, ++i) {
        while (feedback.size() > kFeedbackLag) {
            const Pending& p = feedback.front();
            slots.observe(p.requested, p.earliest, p.actual, p.margin, step, vsync / 2);
            feedback.pop_front();
        }
        PresentClock clock;
        clock.by_moment = true;
        clock.moment = moment;
        clock.grid = grid;
        clock.vsync = vsync;
        clock.step = step;
        const std::int64_t slot = slots.place(clock);
        if (slots.take_skip()) {
            moment += step;
            ++result.skips;
        }
        double share = 0.6;
        bool hitch = false;
        if (load == Load::Varying) {
            share = 0.35 + 0.5 * random.next();
        } else if (load == Load::Spikes) {
            share = 0.4 + 0.2 * random.next();
            if (i % 700u == 350u) {
                share = 2.5;  // a shader compiled, a stage loading
                hitch = true;
            }
        }
        hitch_left = hitch ? 4u : (hitch_left > 0u ? hitch_left - 1u : 0u);
        const std::int64_t ready = std::max(clock.moment + kCpuNs, gpu_free) + std::llround(share * static_cast<double>(step));
        gpu_free = ready;
        std::int64_t earliest = vsync_at_or_after(ready + kLatchNs);
        if (last_actual != 0) earliest = std::max(earliest, last_actual + vsync);
        const std::int64_t actual = std::max(earliest, vsync_at_or_after(slot - vsync / 2));
        last_actual = actual;
        feedback.push_back({slot, earliest, actual, slot - (ready + kLatchNs)});
        shown.push_back({actual - clock.moment, slots.depth_used(), hitch_left > 0u});
        result.depth_most = std::max(result.depth_most, slots.depth_used());
    }
    const auto per_second = static_cast<std::size_t>(1e9 / static_cast<double>(step));
    for (std::size_t k = std::max<std::size_t>(5u * per_second, 1u); k < shown.size(); ++k) {
        if (shown[k].depth != shown[k - 1].depth) {
            if (k >= shown.size() - 30u * per_second) ++result.changes_late;
            continue;
        }
        if (std::llabs(shown[k].latency - shown[k - 1].latency) > vsync / 2) {
            ++result.uneven;
            if (shown[k].hitch || shown[k - 1].hitch) ++result.uneven_hitch;
        }
    }
    result.latency_last = shown.back().latency;
    return result;
}

void check_rate(double hz, unsigned slot_vsyncs) {
    const std::int64_t vsync = std::llround(1e9 / hz);
    const std::int64_t step = vsync * slot_vsyncs;
    for (Load load : {Load::Steady, Load::Varying, Load::Spikes}) {
        const Result r = run(hz, slot_vsyncs, load);
        auto fail = [&](const char* what) {
            std::printf("FAIL: %s (%.2f Hz, a picture every %u vsyncs, %s load): uneven %u (at a hitch %u), depth "
                        "changes in the last 30 s %u, skips %u, depth at most %.2f ms, latency at the end %.2f ms\n",
                        what, hz, slot_vsyncs, load_name(load), r.uneven, r.uneven_hitch, r.changes_late, r.skips,
                        r.depth_most / 1e6, r.latency_last / 1e6);
            ++failures;
        };
        if (r.uneven != r.uneven_hitch) fail("a picture shown unevenly away from a hitch");
        if (load != Load::Spikes && r.uneven != 0u) fail("a picture shown unevenly under a load the GPU keeps up with");
        if (load != Load::Spikes && r.changes_late != 0u) fail("the depth still changing after 30 s");
        if (r.depth_most > 4 * galaxy::kGameStepNs + vsync) fail("a depth over four frames");
        if (load == Load::Steady) {
            // As low as the GPU's work allows: a calm stretch steps down while each picture is ready a present period
            // and 2 ms before the latch.
            const std::int64_t least = (PresentSlots::kLeastDepthNs + vsync - 1) / vsync * vsync;
            const std::int64_t ready = kCpuNs + std::llround(0.6 * static_cast<double>(step)) + kLatchNs;
            const std::int64_t calm = (ready + step + 2'000'000 + vsync - 1) / vsync * vsync;
            if (r.latency_last > std::max(least, calm)) fail("the depth not down to what the GPU's work allows");
        }
    }
}

// The game's own frames on a display whose rate is a multiple of 60: each a frame period after the one before.
void check_frames(double hz) {
    const std::int64_t vsync = std::llround(1e9 / hz);
    const std::int64_t period = galaxy::kGameStepNs / vsync * vsync;
    PresentSlots slots;
    std::int64_t last = 0;
    unsigned uneven = 0;
    for (unsigned i = 0; i < 600u; ++i) {
        PresentClock clock;
        clock.frame_start = 1'000'000'000 + static_cast<std::int64_t>(i) * period;
        clock.period = period;
        clock.vsync = vsync;
        clock.step = period;
        const std::int64_t slot = slots.place(clock);
        if (i > 0u && slot - last != period) ++uneven;
        last = slot;
    }
    if (uneven != 0u) {
        std::printf("FAIL: game frames not a frame period apart (%.2f Hz): %u\n", hz, uneven);
        ++failures;
    }
}

// A queue that keeps the newest picture alone (MAILBOX; Android's async BufferQueue): a picture waits until the first
// refresh it was ready 2 ms before and its asked-for time has come, and a present replaces the one waiting. Pictures one
// refresh apart at 120 Hz, each ready up to 60 % of a refresh late. Untimed, nearly every one is shown; held for a slot two
// refreshes ahead (the least depth at 120 Hz), each is replaced by the next before its slot: the rule presents_timed keeps.
unsigned shown_by_newest_picture_queue(bool timed) {
    const std::int64_t vsync = std::llround(1e9 / 120.0);
    const std::int64_t grid = 1'000'000'000;
    Random random;
    struct Waiting {
        std::int64_t ready, wanted;
    };
    bool waiting = false;
    Waiting picture{};
    unsigned shown = 0;
    std::int64_t refresh = grid;
    for (unsigned i = 0; i < 1200u; ++i) {
        const std::int64_t ready = grid + static_cast<std::int64_t>(i) * vsync +
                                   static_cast<std::int64_t>(random.next() * 0.6 * static_cast<double>(vsync));
        // The refreshes before this present latch what waits.
        for (; refresh < ready; refresh += vsync) {
            if (waiting && picture.ready <= refresh - kLatchNs && picture.wanted <= refresh) {
                ++shown;
                waiting = false;
            }
        }
        picture = {ready, timed ? ready + 2 * vsync : 0};
        waiting = true;  // in place of any picture still waiting
    }
    return shown;
}

void check_swap_queues() {
    static_assert(presents_timed(SwapQueue::EveryPicture) && !presents_timed(SwapQueue::NewestPicture) &&
                  !presents_timed(SwapQueue::Tearing));
    static_assert(pictures_on_refreshes(SwapQueue::EveryPicture) && pictures_on_refreshes(SwapQueue::NewestPicture) &&
                  !pictures_on_refreshes(SwapQueue::Tearing));
    const unsigned untimed = shown_by_newest_picture_queue(false);
    const unsigned timed = shown_by_newest_picture_queue(true);
    if (untimed < 1080u || timed * 2u >= untimed) {
        std::printf("FAIL: a newest-picture queue showed %u of 1200 untimed and %u timed\n", untimed, timed);
        ++failures;
    }
}

}  // namespace

// After a second of late presents, calm ones: the time until the depth comes down again is the same at every rate (it
// was counted in presents: at 240 Hz a quarter of the time at 60).
std::int64_t ms_to_step_down(double hz) {
    const std::int64_t vsync = std::llround(1e9 / hz);
    const std::int64_t grid = 1'000'000'000;
    PresentSlots slots;
    std::int64_t raised = 0;
    for (std::int64_t moment = grid;; moment += vsync) {
        PresentClock clock;
        clock.by_moment = true;
        clock.moment = moment;
        clock.grid = grid;
        clock.vsync = vsync;
        clock.step = vsync;
        const std::int64_t slot = slots.place(clock);
        const std::int64_t depth = slots.depth_used();
        raised = std::max(raised, depth);
        if (slots.take_skip()) {
            moment += vsync;  // a picture fewer: the depth comes down a vsync
        }
        if (moment - grid < 1'500'000'000) {
            // Late by a vsync (missed by less than two frame periods: the device's pace).
            slots.observe(slot, slot + vsync, slot + vsync, 0, vsync, vsync / 2);
        } else {
            slots.observe(slot, slot, slot, 50'000'000, vsync, vsync / 2);
            if (depth < raised) {
                return (moment - grid) / 1'000'000;
            }
        }
        if (moment - grid > 120'000'000'000) {
            return -1;
        }
    }
}

void check_windows_by_time() {
    const std::int64_t at60 = ms_to_step_down(60.0);
    for (double hz : {120.0, 144.0, 240.0, 360.0}) {
        const std::int64_t ms = ms_to_step_down(hz);
        // One window (a second) of difference at most: windows start at different presents.
        if (at60 < 0 || ms < 0 || std::llabs(ms - at60) > 1100) {
            std::printf("FAIL: the depth comes down after %lld ms at %.0f Hz, %lld ms at 60 Hz\n",
                        static_cast<long long>(ms), hz, static_cast<long long>(at60));
            ++failures;
        }
    }
}

int main() {
    check_swap_queues();
    check_windows_by_time();
    // Refresh rates that are and are not multiples of 60, fractional ones, and pictures one and two vsyncs apart.
    const double rates[] = {59.94, 60.0, 72.0, 75.0, 90.0, 100.0, 119.88, 120.0, 143.98, 144.0, 165.0, 170.0, 180.0,
                            185.0, 200.0, 240.0, 360.0};
    for (double hz : rates) {
        check_rate(hz, 1u);
        if (hz / 2.0 > 61.0) check_rate(hz, 2u);
    }
    for (double hz : {60.0, 120.0, 180.0, 240.0}) check_frames(hz);
    if (failures != 0) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("present slots: all passed\n");
    return 0;
}
