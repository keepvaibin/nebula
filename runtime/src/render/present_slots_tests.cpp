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

// What VK_GOOGLE_display_timing tells of a present as Android's loader works it out (swapchain.cpp TimingInfo::calculate):
// the margin from the picture being ready to the compositor's latch of the refresh that showed it, and the earliest
// refresh, moved back a refresh at a time (the refresh taken off the margin each time) while the margin is over a refresh
// and that refresh is still after the latch.
struct Told {
    std::int64_t earliest, margin;
};
Told told(std::int64_t actual, std::int64_t ready, std::int64_t latch, std::int64_t vsync) {
    const std::int64_t latched = actual - latch;
    std::int64_t margin = latched - ready;
    std::int64_t early = actual;
    while (margin > vsync && early - vsync > latched) {
        early -= vsync;
        margin -= vsync;
    }
    return {early, margin};
}

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
        std::int64_t requested, earliest, actual, need;
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
            slots.observe(p.requested, p.earliest, p.actual, p.need, step, vsync / 2);
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
        const Told t = told(actual, ready, kLatchNs, vsync);
        feedback.push_back({slot, earliest, actual, PresentSlots::need_of(t.earliest, t.margin, clock.moment)});
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
            // What the GPU's work asks (a game that does not tell its moments: the depth moves at every window): the
            // least whole vsync at or above the time from the moment to the latch the picture was ready for, with the
            // 1 ms guard, and never under the least.
            const std::int64_t least = (PresentSlots::kLeastDepthNs + vsync - 1) / vsync * vsync;
            const std::int64_t need = kCpuNs + std::llround(0.6 * static_cast<double>(step)) + kLatchNs;
            const std::int64_t asked = (need + PresentDepth::kGuardNs + vsync - 1) / vsync * vsync;
            if (r.latency_last != std::max(least, asked)) fail("the depth not what the GPU's work asks");
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

// After a second of late presents, calm ones, in a game that does not tell its moments (the depth moves at every window):
// the time until the depth comes down again is the same at every rate (windows are seconds of shown times, not counts of
// presents: at 240 Hz a count would be a quarter of the time at 60).
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
            // Late by a vsync (missed by less than two frame periods: the device's pace): it needed a vsync more.
            slots.observe(slot, slot + vsync, slot + vsync, depth + vsync, vsync, vsync / 2);
        } else {
            slots.observe(slot, slot, slot, 5'000'000, vsync, vsync / 2);
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

// The depth's moments (brief AD): in play it holds, whatever the needs, but for one step up after a window of late
// presents, once a stretch of play; where it may move (a closed wipe, a scene loading) it goes to what the needs ask, down
// as well as up; a load's worst is kept for the next four loads; a stall's need sizes nothing; a game that does not tell
// moves at every window. Depths in refreshes of 240 Hz.
void check_moments() {
    const std::int64_t vsync = 4'166'667;
    const auto refreshes = [vsync](std::int64_t ns) { return static_cast<long long>((ns + vsync / 2) / vsync); };
    const auto fail = [&](const char* what, std::int64_t got, long long wanted) {
        std::printf("FAIL: depth moments: %s: %lld refreshes, %lld wanted\n", what, refreshes(got), wanted);
        ++failures;
    };
    std::int64_t t = 1'000'000'000;
    // Presents a vsync apart for `ns`, each needing `need` and shown `late` after its slot.
    const auto run = [&](PresentDepth& depth, std::int64_t ns, std::int64_t need, std::int64_t late) {
        for (const std::int64_t end = t + ns; t < end; t += vsync) {
            (void)depth.depth(3 * vsync, vsync, 8 * vsync, vsync);
            depth.observe(t, t + late, t + late, need, vsync, vsync / 2);
        }
        return depth.depth(3 * vsync, vsync, 8 * vsync, vsync);
    };
    PresentDepth depth;
    depth.allow(false, false);  // play
    if (const auto d = run(depth, 9'000'000'000, vsync, 0); refreshes(d) != 3) fail("calm play does not lower it", d, 3);
    depth.allow(true, true);  // a closed wipe
    // 4.17 ms and the 1 ms guard: two refreshes.
    if (const auto d = run(depth, 1'500'000'000, vsync, 0); refreshes(d) != 2) fail("a wipe takes it to the needs", d, 2);
    depth.allow(false, false);
    // In play late by two refreshes (needing 3.5): one step up, and no more in the same stretch.
    if (const auto d = run(depth, 3'000'000'000, 7 * vsync / 2, 2 * vsync); refreshes(d) != 3) fail("one step up in play", d, 3);
    if (const unsigned steps = depth.take_steps_in_play(); steps != 1u) fail("one step up is one event", steps * vsync, 1);
    depth.allow(true, true);
    // The next wipe: the play's needs within the last 8 s (3.5 refreshes and the guard: 4).
    if (const auto d = run(depth, 1'500'000'000, vsync, 0); refreshes(d) != 4) fail("the wipe after holds play's needs", d, 4);
    depth.allow(false, false);

    // A stall's need (more than two game steps above the depth) sizes nothing where the depth may move.
    PresentDepth stalled;
    stalled.allow(true, true);
    run(stalled, 1'500'000'000, vsync, 0);
    if (const auto d = run(stalled, 1'500'000'000, 3 * vsync + 3 * galaxy::kGameStepNs, 0); refreshes(d) != 2)
        fail("a stall's need left out", d, 2);

    // A load's worst is kept for the next four loads: a load needing 5 refreshes, then loads needing 1.
    PresentDepth loads;
    loads.allow(true, true);
    if (const auto d = run(loads, 2'000'000'000, 5 * vsync, 0); refreshes(d) != 6) fail("a load's need", d, 6);
    for (int load = 1; load <= 5; ++load) {
        loads.allow(false, false);
        run(loads, 9'000'000'000, vsync, 0);
        loads.allow(true, true);
        const std::int64_t d = run(loads, 1'500'000'000, vsync, 0);
        if (refreshes(d) != (load <= 4 ? 6 : 2)) fail(load <= 4 ? "the load kept for four loads" : "the fifth load after it forgets it", d,
                                                      load <= 4 ? 6 : 2);
    }

    // Not told: at every window to the needs, at once.
    PresentDepth untold;
    if (const auto d = run(untold, 1'500'000'000, 7 * vsync / 2, 2 * vsync); refreshes(d) != 4) fail("not told: to the needs at once", d, 4);
    if (const unsigned steps = untold.take_steps_in_play(); steps != 0u) fail("not told: no step in play", steps * vsync, 0);
}

// The need is the same however late a present was shown (a refresh or two after the earliest it could have been: the
// depth plant, --diagnostics pace-depth, asks one later): Android tells the margin against the earliest refresh, and from
// the refresh that showed it the need would be the depth asked.
void check_need_of() {
    const std::int64_t vsync = std::llround(1e9 / 60.0);
    const std::int64_t latch = vsync + 2'000'000;
    const std::int64_t moment = 1'000'000'000;
    const std::int64_t ready = moment + 12'000'000;
    const std::int64_t wanted = ready + latch - moment;
    const std::int64_t earliest = moment + 2 * vsync;  // the first refresh latched after `ready`
    for (std::int64_t later = 0; later <= 2; ++later) {
        const Told t = told(earliest + later * vsync, ready, latch, vsync);
        const std::int64_t need = PresentSlots::need_of(t.earliest, t.margin, moment);
        const std::int64_t by_shown = PresentSlots::need_of(earliest + later * vsync, t.margin, moment);
        if (need != wanted || (later > 0 && by_shown == wanted)) {
            std::printf("FAIL: the need of a present shown %lld refreshes late: %.2f ms (by the shown refresh %.2f), %.2f wanted\n",
                        static_cast<long long>(later), need / 1e6, by_shown / 1e6, wanted / 1e6);
            ++failures;
        }
    }
}

// G10.5's proof 1 on a simulated display that latches a picture a refresh and 2 ms before the refresh it shows (the
// compositor latches at the vsync before), pictures a vsync apart each ready a set time after its moment, the presentation
// times back three presents later, and two pictures due at one refresh: the newer shown (the queue drops the older).
struct Stage {
    explicit Stage(double hz) : vsync(std::llround(1e9 / hz)), latch(vsync + 2'000'000) {}
    std::int64_t vsync, latch;
    std::int64_t grid = 1'000'000'000;
    std::int64_t moment = 1'000'000'000;
    PresentSlots slots;
    struct Pending {
        std::int64_t requested, earliest, actual, need;
    };
    std::deque<Pending> feedback;
    unsigned late = 0;
    unsigned steps = 0;
    unsigned quiet_late = 0;
    std::int64_t deep_ns = 0;
    // `covered`: 1 a closed wipe or a scene loading, 0 play, -1 a game that does not tell (lead_covered).
    void play(std::int64_t ns, std::int64_t ready_ns, int covered) {
        for (const std::int64_t end = moment + ns; moment < end; moment += vsync) {
            slots.allow(covered != 0, covered == 1);
            while (feedback.size() > kFeedbackLag) {
                const Pending& p = feedback.front();
                slots.observe(p.requested, p.earliest, p.actual, p.need, vsync, vsync / 2);
                feedback.pop_front();
            }
            steps += slots.take_steps_in_play();
            quiet_late += slots.take_quiet_late();
            deep_ns += slots.take_deep_ns();
            PresentClock clock;
            clock.by_moment = true;
            clock.moment = moment;
            clock.grid = grid;
            clock.vsync = vsync;
            clock.step = vsync;
            const std::int64_t slot = slots.place(clock);
            const std::int64_t reference = slots.reference();
            if (slots.take_skip()) moment += vsync;
            const std::int64_t ready = clock.moment + ready_ns;
            const std::int64_t earliest = grid + (ready + latch - grid + vsync - 1) / vsync * vsync;
            const std::int64_t actual = std::max(earliest, slot);
            if (actual > slot + vsync / 2) ++late;
            const Told t = told(actual, ready, latch, vsync);
            feedback.push_back({slot, t.earliest, actual, PresentSlots::need_of(t.earliest, t.margin, reference)});
        }
    }
    [[nodiscard]] long long refreshes() const { return (slots.depth_used() + vsync / 2) / vsync; }
};

void check_ready_time_proofs() {
    constexpr std::int64_t s = 1'000'000'000;
    const auto fail = [](const char* what, long long got, long long wanted) {
        std::printf("FAIL: G10.5 proof 1: %s: %lld, %lld wanted\n", what, got, wanted);
        ++failures;
    };
    // (a) A ready time of 12 ms at 60 Hz: two refreshes (12 + 18.67 + the 1 ms guard). A burst of 30 ms in play steps it
    // up once to three; play holds three; the next moment it may move, 10 s after the burst ended, takes it back to two.
    // The margin rule before G10.5 held three for good here: Android's margin is under a refresh (its loader takes whole
    // refreshes off it), never the refresh and 2 ms the rule stepped down at.
    {
        Stage st(60.0);
        st.play(3 * s, 12'000'000, 1);  // the boot's load
        if (st.refreshes() != 2) fail("(a) after the boot's load, refreshes", st.refreshes(), 2);
        st.play(5 * s, 12'000'000, 0);
        st.play(5 * s, 30'000'000, 0);
        if (st.refreshes() != 3 || st.steps != 1u) fail("(a) the burst in play: refreshes (one step up)", st.refreshes() * 10 + st.steps, 31);
        st.play(8 * s + s / 2, 12'000'000, 0);
        if (st.refreshes() != 3) fail("(a) play after the burst holds, refreshes", st.refreshes(), 3);
        st.play(s + s / 2, 12'000'000, 1);  // a scene change under a closed wipe
        if (st.refreshes() != 2) fail("(a) the wipe 10 s after the burst: refreshes", st.refreshes(), 2);
        const unsigned late = st.late;
        st.play(5 * s, 12'000'000, 0);
        if (st.late != late || st.steps != 1u) fail("(a) play after: late pictures", st.late - late, 0);
        // A game that does not tell: the depth follows the needs and is back within 10 s too.
        Stage untold(60.0);
        untold.play(3 * s, 12'000'000, -1);
        untold.play(5 * s, 30'000'000, -1);
        if (untold.refreshes() != 3) fail("(a) not told, the burst: refreshes", untold.refreshes(), 3);
        untold.play(10 * s, 12'000'000, -1);
        if (untold.refreshes() != 2) fail("(a) not told, 10 s after the burst: refreshes", untold.refreshes(), 2);
    }
    // (b) A ready time of 20 ms at 60 Hz: three refreshes (20 + 18.67 + 1 = 39.67 ms), above two game steps: the fault
    // state, held, every second of it counted (the [present-timing] line names it), no picture late.
    {
        Stage st(60.0);
        st.play(3 * s, 20'000'000, 1);
        const unsigned late = st.late;
        st.play(10 * s, 20'000'000, 0);
        if (st.refreshes() != 3) fail("(b) refreshes", st.refreshes(), 3);
        if (st.deep_ns < 11 * s) fail("(b) seconds counted above two steps", st.deep_ns / s, 12);
        if (st.late != late) fail("(b) late pictures at three refreshes", st.late - late, 0);
    }
    // (c) A mode change and a stage load do not deepen: late pictures in the 2 s after either neither step it up nor size
    // the next moment's depth; they are counted. Past the 2 s late pictures step it up again.
    {
        Stage st(60.0);
        st.play(3 * s, 12'000'000, 1);
        st.play(3 * s, 12'000'000, 0);
        for (const char* what : {"a mode change", "a stage load"}) {
            st.slots.quiet(st.moment);
            const unsigned counted = st.quiet_late;
            st.play(s + s / 2, 30'000'000, 0);
            st.play(3 * s, 12'000'000, 0);
            if (st.refreshes() != 2 || st.steps != 0u || st.quiet_late == counted) {
                std::printf("FAIL: G10.5 proof 1 (c): after %s: %lld refreshes, %u steps, %u late counted\n", what, st.refreshes(),
                            st.steps, st.quiet_late - counted);
                ++failures;
            }
        }
        st.play(s + s / 2, 12'000'000, 1);
        if (st.refreshes() != 2) fail("(c) the wipe after: refreshes", st.refreshes(), 2);
        st.play(3 * s, 12'000'000, 0);
        st.play(3 * s, 30'000'000, 0);
        if (st.refreshes() != 3 || st.steps != 1u) fail("(c) a burst past the 2 s: refreshes (one step up)", st.refreshes() * 10 + st.steps, 31);
    }
}

int main() {
    check_swap_queues();
    check_windows_by_time();
    check_moments();
    check_need_of();
    check_ready_time_proofs();
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
