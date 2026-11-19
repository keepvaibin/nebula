// SPDX-License-Identifier: GPL-3.0-only
// Tests of the frame schedule (frame_schedule.h, I-016) on simulated displays: the game's 60 steps a second shown at
// present rates that are and are not multiples of 60, with the display's vblanks timed and not, and an unlimited rate.
// The motion is even when every picture shows the game's time a constant delay before its slot (content time linear
// in present time); the slots of a timed display are its vblanks. The governors, a stall and the walk's history
// pairing are tested on their own. These are the rules the schedule follows today; E6b changes some of them and these
// tests with them.
#include "galaxy/render/frame_schedule.h"

#include "galaxy/game_step.h"
#include "galaxy/render/render_config.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

using galaxy::kGameStepRate;
using namespace galaxy::render;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAILED: %s\n", what);
        ++failures;
    }
}

// The clock's ticks a second, as Windows' performance counter.
constexpr std::int64_t kFrequency = 10'000'000;
// One game step.
constexpr double kStep = static_cast<double>(kFrequency) / kGameStepRate;

struct SimDisplay final : ScheduleDisplay {
    double rate = 120.0;
    bool vsync_on = true;
    bool timed = true;             // the vblanks are timed, pictures show whole from a refresh on
    std::int64_t vblank0 = 12'345;
    std::int64_t period = kFrequency / 120;
    double gpu_ms = 0.0;
    bool temporal = false;
    bool bench = false;
    unsigned suspended = 0;
    unsigned resets = 0;

    ScheduleSettings settings() const override { return {rate, vsync_on}; }
    bool vblank_grid(std::int64_t& vblank, std::int64_t& grid) const override {
        if (!timed) return false;
        vblank = vblank0;
        grid = period;
        return true;
    }
    double generated_gpu_ms() const override { return gpu_ms; }
    bool temporal_upscaling() const override { return temporal; }
    void suspend_temporal_upscaling(double) override { ++suspended; }
    void reset_generated_gpu_ms() override { ++resets; }
    bool benchmark() const override { return bench; }
};

struct Shown {
    SchedulePicture picture;
    std::int64_t step = 0;  // its frame's step moment
    std::int64_t frame = 0; // its sequence's index
    // The game time it shows: the frame's own at t = 1, else t steps on from the step before.
    [[nodiscard]] double content() const {
        const double t = picture.kind == SchedulePicture::Kind::Frame ? 1.0 : picture.t;
        return static_cast<double>(step) + t * kStep;
    }
};

// The render loop of RenderCore, on a simulated clock: each game frame's present arrives `late` after its step's
// moment; the playing sequence's slots are served up to it; it replaces the sequence and starts its own, and the loop
// asks for a picture at once. A generated picture costs `cost` of the render thread's time.
struct Run {
    std::vector<Shown> shown;
    std::vector<std::int64_t> arrivals;  // each sequence's arrival (frame_qpc)
    std::uint64_t superseded = 0;
};

Run run(FrameSchedule& schedule, int frames, std::int64_t first_step, const std::vector<std::int64_t>& late,
        std::int64_t cost = kFrequency / 2000, bool with_step = true, std::uint64_t* waited = nullptr,
        double step_spacing = kStep) {
    Run result;
    std::int64_t now = first_step;
    std::uint64_t waited_total = 0;
    const auto serve = [&](std::int64_t until, std::int64_t index) {
        while (schedule.sequence().active && schedule.slot_due() <= until) {
            now = std::max(now, schedule.slot_due());
            const SchedulePicture picture = schedule.next(now, false);
            if (picture.kind == SchedulePicture::Kind::None) break;
            result.shown.push_back({picture, schedule.sequence().step_qpc, index});
            if (picture.kind == SchedulePicture::Kind::Generated) {
                schedule.generated_drawn(static_cast<std::uint64_t>(cost), true);
                now += cost;
            }
        }
    };
    for (int k = 0; k < frames; ++k) {
        const auto step = first_step + static_cast<std::int64_t>(std::llround(k * step_spacing));
        const std::int64_t asked = step + late[static_cast<std::size_t>(k) % late.size()];
        serve(asked, k - 1);
        now = std::max(now, asked);
        if (waited != nullptr) waited_total += *waited;
        if (schedule.supersede()) ++result.superseded;
        schedule.start(now, asked, with_step ? step : 0, waited_total, false);
        result.arrivals.push_back(schedule.sequence().frame_qpc);
        const SchedulePicture picture = schedule.next(now, false);
        if (picture.kind != SchedulePicture::Kind::None) {
            result.shown.push_back({picture, schedule.sequence().step_qpc, k});
            if (picture.kind == SchedulePicture::Kind::Generated) {
                schedule.generated_drawn(static_cast<std::uint64_t>(cost), true);
                now += cost;
            }
        }
    }
    return result;
}

// After the first `skip` pictures: the spread of (content - slot), in ticks, and whether the slots rise by at least
// `least` each. Counted apart: a frame later than the delay is held at t = 1 until it comes (FrameSchedule::start),
// and a slot between a frame's present and its arrival shows t = 0, the frame before, early (N-1084, E6b's to fix).
struct Evenness {
    double spread = 0.0;
    bool rising = true;
    bool on_grid = true;
    std::size_t held = 0;
    std::size_t early = 0;
};

Evenness evenness(const Run& r, std::size_t skip, std::int64_t least, const SimDisplay* grid) {
    Evenness e;
    double low = std::numeric_limits<double>::max();
    double high = std::numeric_limits<double>::lowest();
    for (std::size_t i = skip; i < r.shown.size(); ++i) {
        const Shown& s = r.shown[i];
        if (i > skip && s.picture.slot - r.shown[i - 1].picture.slot < least) e.rising = false;
        if (grid != nullptr && (s.picture.slot - grid->vblank0) % grid->period != 0) e.on_grid = false;
        if (s.picture.kind == SchedulePicture::Kind::Frame && s.picture.t > 1.0 + 1e-3) {
            ++e.held;
            continue;
        }
        if (s.picture.kind == SchedulePicture::Kind::Generated && s.picture.t == 0.0) {
            ++e.early;
            continue;
        }
        const double offset = s.content() - static_cast<double>(s.picture.slot);
        low = std::min(low, offset);
        high = std::max(high, offset);
    }
    e.spread = high - low;
    return e;
}

void test_vblanks_per_slot() {
    check(vblanks_per_slot(120.0, 120.0) == 1, "120 Hz on 120: every vblank");
    check(vblanks_per_slot(240.0, 120.0) == 2, "120 Hz on 240: every second vblank");
    check(vblanks_per_slot(165.0, 120.0) == 2, "120 Hz on 165: every second vblank (82.5 Hz)");
    check(vblanks_per_slot(144.0, 144.0) == 1, "144 Hz on 144");
    check(vblanks_per_slot(240.0, 61.0) == 3, "the rate kept above 60: 80 Hz on 240, not 60");
    check(vblanks_per_slot(120.0, std::numeric_limits<double>::infinity()) == 1, "an unlimited rate: every vblank");
}

// Timed vblanks: every slot a vblank, never two pictures in one, and the content's time a constant delay before the
// slot (within the step delay's 0.1 ms steps), at every refresh rate above 60.
void test_timed_vblanks() {
    for (const double refresh : {120.0, 144.0, 165.0, 240.0}) {
        SimDisplay display;
        display.rate = refresh;
        display.period = static_cast<std::int64_t>(std::llround(static_cast<double>(kFrequency) / refresh));
        FrameSchedule schedule(display, kFrequency);
        schedule.configure(interpolated_frames_per_game_frame(refresh), 0);
        const Run r = run(schedule, 600, kFrequency, {kFrequency / 500});  // 2 ms late, steady
        const Evenness e = evenness(r, r.shown.size() / 2, display.period, &display);
        char what[160];
        std::snprintf(what, sizeof what, "%.0f Hz timed: every slot a vblank", refresh);
        check(e.on_grid, what);
        std::snprintf(what, sizeof what, "%.0f Hz timed: one picture a vblank at most", refresh);
        check(e.rising, what);
        std::snprintf(what, sizeof what, "%.0f Hz timed: content time linear in present time (spread %.0f ticks)",
                      refresh, e.spread);
        check(e.spread <= kFrequency / 5000.0, what);
        // About the display's rate of pictures, all but the start's.
        const double seconds = 600.0 / kGameStepRate;
        std::snprintf(what, sizeof what, "%.0f Hz timed: %zu pictures in %.0f s", refresh, r.shown.size(), seconds);
        check(static_cast<double>(r.shown.size()) > refresh * seconds * 0.97, what);
    }
}

// A tearing swap chain at 120 Hz, a multiple of 60: the slot clock locks to each arrival with lead 1/2, so each game
// frame shows one picture at t = 1/2 and then itself at t = 1, on its own slot, and no sequence is replaced.
void test_locked_rate() {
    SimDisplay display;
    display.timed = false;
    display.vsync_on = false;
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(interpolated_frames_per_game_frame(120.0), 0);
    const Run r = run(schedule, 300, kFrequency, {kFrequency / 500});
    std::size_t generated = 0, frames = 0, wrong = 0;
    for (std::size_t i = r.shown.size() / 2; i < r.shown.size(); ++i) {
        const SchedulePicture& p = r.shown[i].picture;
        if (p.kind == SchedulePicture::Kind::Generated) {
            ++generated;
            wrong += std::abs(p.t - 0.5) > 1e-3 ? 1u : 0u;
        } else {
            ++frames;
            wrong += std::abs(p.t - 1.0) > 1e-3 ? 1u : 0u;
        }
    }
    check(wrong == 0u, "locked 120 Hz: pictures at t = 1/2 and t = 1 exactly");
    check(generated + 1u >= frames && frames + 1u >= generated, "locked 120 Hz: one generated picture per frame");
    check(r.superseded == 0u, "locked 120 Hz: no sequence replaced");
}

// A tearing swap chain at 144 Hz: a free-running clock (no lead), the content's time linear in present time across
// frames, each sequence replaced by the next as its normal end.
void test_free_running() {
    SimDisplay display;
    display.timed = false;
    display.vsync_on = false;
    display.rate = 144.0;
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(interpolated_frames_per_game_frame(144.0), 0);
    const Run r = run(schedule, 600, kFrequency, {kFrequency / 500});
    const Evenness e = evenness(r, r.shown.size() / 2, kFrequency / 145, nullptr);
    check(e.rising, "free-running 144 Hz: slots a slot apart");
    char what[120];
    std::snprintf(what, sizeof what, "free-running 144 Hz: content time linear (spread %.0f ticks)", e.spread);
    check(e.spread <= kFrequency / 5000.0, what);
    check(r.superseded > 500u, "free-running 144 Hz: sequences end replaced");
}

// The game's lateness varies: the step clock's delay follows its 97th percentile, so the content keeps its speed.
void test_varying_lateness() {
    SimDisplay display;
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(1u, 0);
    std::vector<std::int64_t> late;
    for (int i = 0; i < 100; ++i) late.push_back(kFrequency / 1000 + (i * 37 % 100) * (kFrequency / 100'000));  // 1-2 ms
    const Run r = run(schedule, 1200, kFrequency, late);
    const Evenness e = evenness(r, r.shown.size() / 2, display.period, &display);
    char what[120];
    std::snprintf(what, sizeof what, "1-2 ms lateness at 120 Hz: content time linear (spread %.0f ticks)", e.spread);
    check(e.spread <= kFrequency / 2000.0, what);
    std::snprintf(what, sizeof what, "1-2 ms lateness at 120 Hz: few frames held (%zu of %zu pictures)", e.held,
                  r.shown.size() / 2);
    check(e.held * 20u <= r.shown.size() / 2, what);
    // Today a slot between a frame's present and its arrival shows the frame before, up to the lateness's spread early
    // (N-1084). E6b makes it none and this count zero.
    std::snprintf(what, sizeof what, "1-2 ms lateness at 120 Hz: pictures shown early at t = 0 (%zu)", e.early);
    check(e.early > 0u && e.early * 4u <= r.shown.size() / 2, what);
    const double delay_ms = static_cast<double>(schedule.delay_qpc()) * 1000.0 / kFrequency;
    std::snprintf(what, sizeof what, "the delay covers the lateness's top, plus its guard (%.2f ms)", delay_ms);
    check(delay_ms > 1.8 && delay_ms < 2.4, what);
}

// No step moments: the arrival clock smooths the presents' times to the game's period.
void test_arrival_clock() {
    SimDisplay display;
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(1u, 0);
    std::vector<std::int64_t> late;
    for (int i = 0; i < 64; ++i) late.push_back(kFrequency / 1000 + (i * 29 % 64) * (kFrequency / 64'000));  // 1-2 ms
    const Run r = run(schedule, 600, kFrequency, late, kFrequency / 2000, false);
    double worst = 0.0;
    for (std::size_t i = r.arrivals.size() / 2; i + 1 < r.arrivals.size(); ++i) {
        worst = std::max(worst, std::abs(static_cast<double>(r.arrivals[i + 1] - r.arrivals[i]) - kStep));
    }
    char what[120];
    std::snprintf(what, sizeof what, "arrivals a game step apart within 1/8 of the jitter (worst %.0f ticks)", worst);
    check(worst < kFrequency / 1000.0 / 8.0 + kStep / 60.0, what);
}

// An unlimited rate: no slot clock; each picture is due at once and shows the moment it is drawn for.
void test_unlimited() {
    SimDisplay display;
    display.timed = false;
    display.vsync_on = false;
    display.rate = std::numeric_limits<double>::infinity();
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(kUnboundedGeneratedFrames, 0);
    std::int64_t now = kFrequency;
    schedule.start(now, now, now - kFrequency / 500, 0, false);
    check(schedule.unbounded() && schedule.slot_period() == 0, "unlimited: no slot clock");
    double t_before = -1.0;
    bool linear = true;
    unsigned pictures = 0;
    for (; pictures < 1000; ++pictures) {
        const SchedulePicture p = schedule.next(now, false);
        if (p.kind != SchedulePicture::Kind::Generated) break;
        linear = linear && p.slot == now && p.t >= t_before;
        t_before = p.t;
        schedule.generated_drawn(kFrequency / 1000, true);
        now += kFrequency / 1000;  // a picture a millisecond
    }
    check(linear, "unlimited: each picture at its own moment, t rising");
    check(pictures >= 15u && pictures <= 18u, "unlimited: a picture a millisecond until the frame's moment");
    check(schedule.next(now, false).kind == SchedulePicture::Kind::None, "unlimited: the frame shown once");
}

// A stall skips the slots it missed instead of bunching them.
void test_stall() {
    SimDisplay display;
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(3u, 0);
    display.rate = 240.0;
    display.period = kFrequency / 240;
    const std::int64_t now = kFrequency;
    schedule.start(now, now, 0, 0, false);
    const std::int64_t due = schedule.slot_due();
    (void)schedule.take_counts();
    const SchedulePicture p = schedule.next(due + 5 * display.period, false);
    check(p.kind != SchedulePicture::Kind::None, "a stalled slot is served");
    check(schedule.slot_due() == due + 6 * display.period, "the next slot after the stall, on the grid");
    check(schedule.take_counts().late_slots == 5u, "five slots counted late");
}

// The GPU governor: generated pictures that do not fit in nine tenths of a step are dropped once its warm second and
// 30 sequences have passed, and come back 120 sequences after they fit again.
void test_gpu_governor() {
    SimDisplay display;
    display.gpu_ms = 10.0;  // 15 ms budget at 60 steps: the frame and one picture do not fit
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(1u, kFrequency);
    std::int64_t now = kFrequency;
    unsigned generated = 1;
    int sequences = 0;
    for (; sequences < 200 && generated != 0u; ++sequences) {
        now += static_cast<std::int64_t>(kStep);
        schedule.start(now, now, 0, 0, false);
        generated = schedule.sequence().generated;
    }
    check(generated == 0u, "over budget: no generated pictures");
    check(sequences > 60, "not before the warm second");
    display.gpu_ms = 2.0;
    int back = 0;
    for (; back < 400 && generated == 0u; ++back) {
        now += static_cast<std::int64_t>(kStep);
        schedule.start(now, now, 0, 0, false);
        generated = schedule.sequence().generated;
    }
    check(generated == 1u, "under budget again: the generated picture back");
    check(back >= 120, "not before 120 sequences");
    check(display.suspended == 0u, "no upscaler to suspend");

    SimDisplay upscaled;
    upscaled.gpu_ms = 10.0;
    upscaled.temporal = true;
    FrameSchedule with_upscaler(upscaled, kFrequency);
    with_upscaler.configure(1u, kFrequency);
    now = kFrequency;
    for (int i = 0; i < 100; ++i) {
        now += static_cast<std::int64_t>(kStep);
        with_upscaler.start(now, now, 0, 0, false);
    }
    check(upscaled.suspended > 0u, "temporal upscaling on generated pictures suspended when none fits");
}

// The cadence governor: game frames late by the render thread's generated pictures take them away; late by the game's
// own work (no waits on the render thread), a probe takes them away for a second and gives them back.
void test_cadence_governor() {
    {
        SimDisplay display;
        FrameSchedule schedule(display, kFrequency);
        schedule.configure(1u, 0);
        std::uint64_t waited = static_cast<std::uint64_t>(kStep);
        // Every frame two steps apart (one missed), the game waiting on the render thread a step each time, and each
        // generated picture a step of the render thread's time.
        (void)run(schedule, 200, kFrequency, {kFrequency / 500}, static_cast<std::int64_t>(kStep), true, &waited,
                  2.0 * kStep);
        check(schedule.cadence_cap() == 0u, "misses the generated pictures caused: none left");
        check(schedule.take_counts().cadence_drops >= 1u, "counted as a drop");
    }
    {
        SimDisplay display;
        FrameSchedule schedule(display, kFrequency);
        schedule.configure(1u, 0);
        std::uint64_t waited = 0;
        const Run r = run(schedule, 400, kFrequency, {kFrequency / 500}, kFrequency / 2000, true, &waited, 2.0 * kStep);
        (void)r;
        check(schedule.cadence_cap() == kUnboundedGeneratedFrames, "the game's own misses: generated pictures kept");
        check(schedule.sequence().generated == 1u, "and the last sequence has its picture");
    }
}

// The walk's history: motion from the picture walked before on the same pair of frames, across sequences from the
// frame before's end, and a new history after a cut or a gap.
void test_walk_pairing() {
    SimDisplay display;
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(1u, 0);
    int a = 0, b = 0, c = 0, d = 0;
    schedule.start(kFrequency, kFrequency, 0, 0, false);
    ScheduleWalk w = schedule.walk_time(&a, nullptr, false, 0.5f, true);
    check(w.reset, "nothing walked before: a new history");
    schedule.walked(&a, 0.5f);
    w = schedule.walk_time(&a, nullptr, false, 1.0f, true);
    check(!w.reset && w.t_prev == 0.5f, "the same pair: motion from t = 1/2");
    schedule.walked(&a, 1.0f);
    schedule.start(kFrequency * 2, kFrequency * 2, 0, 0, false);
    w = schedule.walk_time(&b, &a, false, 0.5f, true);
    check(!w.reset && w.t_prev == 0.0f, "the next pair from its frame before's end: t = 0");
    w = schedule.walk_time(&b, &a, true, 0.5f, true);
    check(w.reset, "a camera cut: a new history");
    schedule.walked(&b, 0.75f);
    schedule.start(kFrequency * 3, kFrequency * 3, 0, 0, false);
    w = schedule.walk_time(&c, &b, false, 0.25f, true);
    check(!w.reset && w.t_prev == -0.25f, "a pair replaced at t = 3/4: motion from t - 1 on the next");
    w = schedule.walk_time(&d, &a, false, 0.25f, true);
    check(w.reset, "skipped lists: a new history");
    const ScheduleCounts counts = schedule.take_counts();
    check(counts.history_carried == 1u && counts.history_resets == 3u, "history carried and reset counted");
    schedule.forget();
    check(schedule.walk_time(&c, &b, false, 0.5f, false).reset, "a lost device forgets the pairing");
}

// A new present rate starts both governors over; a rate with no generated pictures leaves no slot clock.
void test_configure() {
    SimDisplay display;
    display.gpu_ms = 10.0;
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(1u, 0);
    check(display.resets == 1u, "a new configuration resets the measured GPU time");
    std::int64_t now = 2 * kFrequency;
    for (int i = 0; i < 100; ++i) {
        now += static_cast<std::int64_t>(kStep);
        schedule.start(now, now, 0, 0, false);
    }
    check(schedule.gpu_cap() == 0u, "capped");
    schedule.configure(1u, now);
    check(schedule.gpu_cap() == 0u && display.resets == 1u, "the same configuration changes nothing");
    schedule.configure(3u, now);
    check(schedule.gpu_cap() == kUnboundedGeneratedFrames && display.resets == 2u, "a new one starts over");
    schedule.configure(0u, now);
    check(schedule.slot_period() == 0 && !schedule.unbounded(), "none: no slot clock");
    schedule.start(now, now, 0, 0, true);
    check(schedule.sequence().generated == 0u, "stopping: no generated pictures");
}

// Rate steps: on a 240 Hz display three generated pictures show 240 Hz, two show 120 Hz (every second vblank) as one
// does; the step down is the most below that present lower, so from three it is two.
void test_rate_steps() {
    SimDisplay display;
    display.rate = 240.0;
    display.period = kFrequency / 240;
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(3u, 0);
    check(std::abs(schedule.rate_with(3u) - 240.0) < 0.5, "three: 240 Hz");
    check(std::abs(schedule.rate_with(2u) - 120.0) < 0.5, "two: every second vblank, 120 Hz");
    check(std::abs(schedule.rate_with(1u) - 120.0) < 0.5, "one: 120 Hz");
    check(schedule.step_down(3u) == 2u, "down from three: two (120 Hz)");
    check(schedule.step_down(2u) == 0u && schedule.step_down(1u) == 0u, "down from two or one: none");
    check(schedule.step_up(0u) == 1u, "up from none: one");
    check(schedule.step_up(1u) == 3u, "up from one: three");
}

}  // namespace

int main() {
    test_vblanks_per_slot();
    test_timed_vblanks();
    test_locked_rate();
    test_free_running();
    test_varying_lateness();
    test_arrival_clock();
    test_unlimited();
    test_stall();
    test_gpu_governor();
    test_cadence_governor();
    test_walk_pairing();
    test_configure();
    test_rate_steps();
    if (failures != 0) {
        std::printf("%d failed\n", failures);
        return 1;
    }
    std::printf("frame schedule: all passed\n");
    return 0;
}
