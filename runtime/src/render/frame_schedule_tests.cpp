// SPDX-License-Identifier: GPL-3.0-only
// Tests of the frame schedule (frame_schedule.h, I-016) on simulated displays: the game's 60 steps a second shown at
// present rates that are and are not multiples of 60, with the display's vblanks timed and not, an unlimited rate and a
// variable refresh. The motion is even when every picture shows the game's time a constant delay before its slot
// (content time linear in present time); the slots of a timed display are its vblanks. A sequence starts when its list
// arrives, its reference its own step's moment plus the step clock's delay (I-004), and a slot before that reference
// shows the sequence before at its own t, never the frame before early (N-1084). The governors, a stall and the walk's
// history pairing are tested on their own.
#include "galaxy/render/frame_schedule.h"

#include "galaxy/game_step.h"
#include "galaxy/render/render_config.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
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

// A display at `refresh` hertz, its present rate the same (VSync on, the frame rate at the refresh or above), its vblanks
// timed or a swap chain that tears.
SimDisplay display_at(double refresh, bool timed) {
    SimDisplay display;
    display.rate = refresh;
    display.timed = timed;
    display.vsync_on = timed;
    display.period = static_cast<std::int64_t>(std::llround(static_cast<double>(kFrequency) / refresh));
    return display;
}

struct Shown {
    SchedulePicture picture;
    std::int64_t step = 0;       // its frame's step moment
    std::int64_t reference = 0;  // its sequence's reference
    double lead = 0.0;
    unsigned generated = 0;      // its sequence's generated pictures
    int frame = 0;               // its list's index
    // The game time it shows: the frame's own at t = 1, else t steps on from the step before.
    [[nodiscard]] double content() const {
        const double t = picture.kind == SchedulePicture::Kind::Frame ? 1.0 : picture.t;
        return static_cast<double>(step) + t * kStep;
    }
    // t of its slot on its own sequence's clock.
    [[nodiscard]] double slot_t() const {
        return static_cast<double>(picture.slot - reference) * kGameStepRate / static_cast<double>(kFrequency) + lead;
    }
};

// What RenderCore does as a list arrives: a sequence still waiting begins first (the playing one replaced), then the new
// one starts. With `alone` the playing one is replaced too, so the new one begins at once.
void arrive(FrameSchedule& schedule, std::int64_t now, std::int64_t arrived, std::int64_t step, bool alone,
            bool stopping = false) {
    if (schedule.waiting()) {
        (void)schedule.supersede();
        schedule.begin_waiting(now);
    }
    if (alone) {
        (void)schedule.supersede();
    }
    schedule.start(now, arrived, step, stopping);
}

// The render loop of RenderCore, on a simulated clock: each step asks for its present `present_late` after its moment
// (the cadence count) and hands its list over `late` after it; the playing sequence's slots are served up to each, the
// list's sequence starts (at once, or waiting for its reference), and the loop asks for a picture at once. A generated
// picture costs `cost` of the render thread's time. `rates`: the display's rate for each list in turn (a variable
// refresh), else the display's own.
struct Run {
    std::vector<Shown> shown;
    std::vector<std::int64_t> references;  // each sequence's reference as it began
    std::vector<std::int64_t> steps;       // each list's step moment
    std::uint64_t superseded = 0;
};

Run run(FrameSchedule& schedule, int frames, std::int64_t first_step, const std::vector<std::int64_t>& late,
        std::int64_t cost = kFrequency / 2000, bool with_step = true, std::uint64_t* waited = nullptr,
        double step_spacing = kStep, SimDisplay* display = nullptr, const std::vector<double>* rates = nullptr) {
    Run result;
    std::int64_t now = first_step;
    std::uint64_t waited_total = 0;
    int playing = -1;
    int waiting = -1;
    const auto record = [&](const SchedulePicture& picture) {
        const ScheduleSequence& s = schedule.sequence();
        result.shown.push_back({picture, s.step_qpc, s.frame_qpc, s.lead, s.generated, playing});
        if (picture.kind == SchedulePicture::Kind::Generated) {
            schedule.generated_drawn(static_cast<std::uint64_t>(cost), true);
            now += cost;
        }
    };
    const auto begin = [&] {
        if (schedule.supersede()) ++result.superseded;
        schedule.begin_waiting(now);
        playing = waiting;
        waiting = -1;
        result.references.push_back(schedule.sequence().frame_qpc);
    };
    const auto draw = [&] {
        SchedulePicture picture = schedule.next(now, false);
        if (picture.kind == SchedulePicture::Kind::Begin) {
            begin();
            picture = schedule.next(now, false);
        }
        if (picture.kind == SchedulePicture::Kind::Generated || picture.kind == SchedulePicture::Kind::Frame) {
            record(picture);
        }
    };
    const auto serve = [&](std::int64_t until) {
        while (schedule.busy() && schedule.slot_due() <= until) {
            now = std::max(now, schedule.slot_due());
            const std::int64_t due = schedule.slot_due();
            const std::size_t count = result.shown.size();
            draw();
            if (result.shown.size() == count && schedule.slot_due() == due && !schedule.waiting()) break;
        }
    };
    for (int k = 0; k < frames; ++k) {
        if (display != nullptr && rates != nullptr) {
            display->rate = (*rates)[static_cast<std::size_t>(k) % rates->size()];
        }
        const auto step = first_step + static_cast<std::int64_t>(std::llround(k * step_spacing));
        result.steps.push_back(step);
        // The step's present request, just after its moment: the cadence count only.
        const std::int64_t asked = step + kFrequency / 5000;
        serve(asked);
        now = std::max(now, asked);
        if (waited != nullptr) waited_total += *waited;
        schedule.note_step(asked, waited_total);
        // Its list, at the end of its work.
        const std::int64_t arrived = step + late[static_cast<std::size_t>(k) % late.size()];
        serve(arrived);
        now = std::max(now, arrived);
        if (schedule.waiting()) begin();
        schedule.start(now, arrived, with_step ? step : 0, false);
        if (schedule.waiting()) {
            waiting = k;
        } else {
            playing = k;
            result.references.push_back(schedule.sequence().frame_qpc);
        }
        draw();
    }
    return result;
}

// After the first `skip` pictures: the spread of (content - slot), in ticks; whether the slots rise by at least `least`
// each; the largest time from a picture's content to its slot. Counted apart: a frame later than the delay, held at
// t = 1 until its list came; and a picture early, showing a moment after its slot's (N-1084: a slot between a frame's
// present and its arrival showed t = 0, the frame before), or a frame shown before its moment.
struct Evenness {
    double spread = 0.0;
    double lag_max = 0.0;
    bool rising = true;
    bool on_grid = true;
    std::size_t held = 0;
    std::size_t early = 0;
    bool frames_once = true;  // no list's frame shown twice
};

Evenness evenness(const Run& r, std::size_t skip, std::int64_t least, const SimDisplay* grid) {
    Evenness e;
    double low = std::numeric_limits<double>::max();
    double high = std::numeric_limits<double>::lowest();
    std::vector<int> frames_shown(r.steps.size() + 1u, 0);
    for (std::size_t i = 0; i < r.shown.size(); ++i) {
        const Shown& s = r.shown[i];
        if (s.picture.kind == SchedulePicture::Kind::Frame && s.frame >= 0 &&
            ++frames_shown[static_cast<std::size_t>(s.frame)] > 1) {
            e.frames_once = false;
        }
        if (i < skip) continue;
        if (i > skip && s.picture.slot - r.shown[i - 1].picture.slot < least) e.rising = false;
        if (grid != nullptr && (s.picture.slot - grid->vblank0) % grid->period != 0) e.on_grid = false;
        if (s.generated != 0u && !s.picture.flush) {
            const bool early_generated = s.picture.kind == SchedulePicture::Kind::Generated &&
                                         (s.picture.t > s.slot_t() + 1e-9 || s.picture.t == 0.0);
            const bool early_frame = s.picture.kind == SchedulePicture::Kind::Frame && s.slot_t() < 1.0 - 1e-3;
            if (early_generated || early_frame) {
                ++e.early;
                continue;
            }
        }
        if (s.picture.kind == SchedulePicture::Kind::Frame && s.picture.t > 1.0 + 1e-3) {
            ++e.held;
            continue;
        }
        const double offset = s.content() - static_cast<double>(s.picture.slot);
        low = std::min(low, offset);
        high = std::max(high, offset);
        e.lag_max = std::max(e.lag_max, -offset);
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
        SimDisplay display = display_at(refresh, true);
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

// A tearing swap chain at 120 Hz, a multiple of 60: the slot clock locks to each reference with lead 1/2, so each game
// frame shows one picture at t = 1/2 and then itself at t = 1, on its own slot, and no sequence is replaced.
void test_locked_rate() {
    SimDisplay display = display_at(120.0, false);
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
    SimDisplay display = display_at(144.0, false);
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

// I-004: a sequence starts at its list's arrival, its reference its own step's moment plus the delay, so a picture
// shows the game the delay after the moment it shows (today's 2 ms lateness: about 2.2 ms), where the present the
// next step asks for put it a whole step and more later.
void test_starts_at_arrival() {
    SimDisplay display = display_at(120.0, true);
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(1u, 0);
    const Run r = run(schedule, 600, kFrequency, {kFrequency / 500});
    double worst = 0.0;
    for (std::size_t i = r.references.size() / 2; i < r.references.size() && i < r.steps.size(); ++i) {
        worst = std::max(worst, static_cast<double>(r.references[i] - r.steps[i]));
    }
    // The delay's estimate moves up and down by its 0.1 ms step around the lateness's top, so a reference is within one
    // step of the delay last set.
    const double delay = static_cast<double>(schedule.delay_qpc()) + kFrequency / 10000.0;
    char what[160];
    std::snprintf(what, sizeof what, "each reference its own step's moment plus the delay (worst %.0f ticks, delay %.0f)",
                  worst, delay);
    check(worst <= delay && delay < kStep / 4.0, what);
    const Evenness e = evenness(r, r.shown.size() / 2, display.period, &display);
    std::snprintf(what, sizeof what, "a picture shows the game at most the delay before its slot (%.0f ticks)", e.lag_max);
    check(e.lag_max <= delay, what);
}

// The game's lateness varies: the step clock's delay follows its 97th percentile, so the content keeps its speed; a slot
// between a list's arrival and its reference shows the sequence before at its own t (N-1084: none early).
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
    std::snprintf(what, sizeof what, "1-2 ms lateness at 120 Hz: no picture shown early (%zu)", e.early);
    check(e.early == 0u, what);
    const double delay_ms = static_cast<double>(schedule.delay_qpc()) * 1000.0 / kFrequency;
    std::snprintf(what, sizeof what, "the delay covers the lateness's top, plus its guard (%.2f ms)", delay_ms);
    check(delay_ms > 1.8 && delay_ms < 2.4, what);
}

// Every rate of file E A.5 and the head's "Any refresh rate" sample points (G2.1 proof 1): the named rates, 300 random
// ones between 40 and 500 Hz from a fixed seed (most not whole numbers), each on timed vblanks and on a swap chain that
// tears, under steady, varying and spiking load. No picture is early, none is two in a vblank, each slot of a timed
// display is a vblank, no list's frame is shown twice; under steady and varying load each picture shows the game at
// most the delay (under half a step) before its slot.
void test_every_rate() {
    std::vector<double> rates{40.0, 48.0, 50.0, 59.94, 60.0, 72.0, 75.0, 82.37, 90.0, 100.0, 119.88, 120.0,
                              144.0, 165.0, 180.0, 200.0, 240.0, 360.0, 480.0, 500.0};
    std::mt19937 random(20261124u);
    std::uniform_real_distribution<double> any(40.0, 500.0);
    for (int i = 0; i < 300; ++i) rates.push_back(any(random));
    std::vector<std::int64_t> steady{kFrequency / 500};
    std::vector<std::int64_t> varying;
    for (int i = 0; i < 64; ++i) varying.push_back(kFrequency / 1000 + (i * 29 % 64) * (kFrequency / 12'800));  // 1-6 ms
    std::vector<std::int64_t> spiking(60, kFrequency / 500);
    spiking[30] = kFrequency / 40;  // 25 ms: a list later than its step
    const std::vector<std::int64_t>* loads[] = {&steady, &varying, &spiking};
    const char* load_names[] = {"steady", "varying", "spiking"};
    unsigned bad = 0;
    for (const double refresh : rates) {
        for (const bool timed : {true, false}) {
            for (int l = 0; l < 3; ++l) {
                SimDisplay display = display_at(refresh, timed);
                FrameSchedule schedule(display, kFrequency);
                const unsigned generated = interpolated_frames_per_game_frame(refresh);
                schedule.configure(generated, 0);
                const Run r = run(schedule, 240, kFrequency, *loads[l]);
                const Evenness e = evenness(r, r.shown.size() / 4, timed ? display.period : 1, timed ? &display : nullptr);
                const bool interpolated = generated != 0u;
                const bool ok = e.early == 0u && e.frames_once && (!interpolated || (e.rising && e.on_grid)) &&
                                (!interpolated || l == 2 || e.lag_max < kStep / 2.0);
                if (!ok && bad++ < 10u) {
                    std::printf("  %.2f Hz %s %s: early %zu, frames once %d, rising %d, on grid %d, lag %.0f ticks\n",
                                refresh, timed ? "timed" : "tearing", load_names[l], e.early, e.frames_once ? 1 : 0,
                                e.rising ? 1 : 0, e.on_grid ? 1 : 0, e.lag_max);
                }
            }
        }
    }
    char what[120];
    std::snprintf(what, sizeof what, "every rate, timed and tearing, three loads: %u of %zu runs wrong", bad,
                  rates.size() * 6u);
    check(bad == 0u, what);
}

// A variable refresh: no timed vblanks, the rate each list sees moving through the display's range (48 to 144 Hz and
// back). No picture is early, the slots rise, each picture shows the game at most the delay before its slot.
void test_variable_refresh() {
    std::vector<double> range;
    for (int i = 0; i < 64; ++i) {
        range.push_back(96.0 + 48.0 * std::sin(static_cast<double>(i) * 0.098));  // 48 to 144 Hz
    }
    SimDisplay display = display_at(144.0, false);
    display.vsync_on = true;
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(interpolated_frames_per_game_frame(144.0), 0);
    const Run r = run(schedule, 600, kFrequency, {kFrequency / 500}, kFrequency / 2000, true, nullptr, kStep, &display,
                      &range);
    const Evenness e = evenness(r, r.shown.size() / 4, 1, nullptr);
    char what[160];
    std::snprintf(what, sizeof what, "variable refresh 48-144 Hz: early %zu, rising %d, lag %.0f ticks", e.early,
                  e.rising ? 1 : 0, e.lag_max);
    check(e.early == 0u && e.rising && e.frames_once && e.lag_max < kStep / 2.0, what);
}

// No step moments: the arrival clock smooths the lists' times to the game's period.
void test_arrival_clock() {
    SimDisplay display;
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(1u, 0);
    std::vector<std::int64_t> late;
    for (int i = 0; i < 64; ++i) late.push_back(kFrequency / 1000 + (i * 29 % 64) * (kFrequency / 64'000));  // 1-2 ms
    const Run r = run(schedule, 600, kFrequency, late, kFrequency / 2000, false);
    double worst = 0.0;
    for (std::size_t i = r.references.size() / 2; i + 1 < r.references.size(); ++i) {
        worst = std::max(worst, std::abs(static_cast<double>(r.references[i + 1] - r.references[i]) - kStep));
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
    arrive(schedule, now, now, now - kFrequency / 500, true);
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
    // A list in before its reference with nothing playing: the next picture waits for the reference, not drawn early.
    arrive(schedule, now, now, now, true);
    const SchedulePicture early = schedule.next(now, false);
    check(early.kind == SchedulePicture::Kind::None && schedule.slot_due() > now && schedule.busy(),
          "unlimited: a slot before the reference waits for it");
    check(schedule.take_counts().waited_slots == 1u, "the wait counted");
}

// A stall skips the slots it missed instead of bunching them.
void test_stall() {
    SimDisplay display;
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(3u, 0);
    display.rate = 240.0;
    display.period = kFrequency / 240;
    const std::int64_t now = kFrequency;
    arrive(schedule, now, now, 0, true);
    const std::int64_t due = schedule.slot_due();
    (void)schedule.take_counts();
    const SchedulePicture p = schedule.next(due + 5 * display.period, false);
    check(p.kind != SchedulePicture::Kind::None, "a stalled slot is served");
    check(schedule.slot_due() == due + 6 * display.period, "the next slot after the stall, on the grid");
    check(schedule.take_counts().late_slots == 5u, "five slots counted late");
}

// A list in before its reference while a sequence plays waits (N-1084); a second list before it began makes it begin;
// a slot past the reference begins it.
void test_waiting() {
    SimDisplay display;
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(1u, 0);
    std::int64_t now = kFrequency;
    arrive(schedule, now, now, now - kFrequency / 500, false);
    check(schedule.sequence().active && !schedule.waiting(), "nothing playing: the first sequence begins at once");
    const std::int64_t first = schedule.sequence().frame_qpc;
    const std::int64_t next_step = now - kFrequency / 500 + static_cast<std::int64_t>(kStep);
    now = next_step + kFrequency / 10'000;  // in 0.1 ms after its step, before its reference
    arrive(schedule, now, now, next_step, false);
    check(schedule.waiting() && schedule.sequence().frame_qpc == first, "a list in early waits; the one before plays on");
    // A slot due before the waiting reference is the playing sequence's.
    SchedulePicture p = schedule.next(schedule.slot_due(), false);
    check(p.kind != SchedulePicture::Kind::Begin, "a slot before the reference: the playing sequence's picture");
    // Past the reference: Begin.
    for (int i = 0; i < 8 && p.kind != SchedulePicture::Kind::Begin; ++i) {
        p = schedule.next(std::max(schedule.slot_due(), now), false);
        if (p.kind == SchedulePicture::Kind::Generated) schedule.generated_drawn(0u, true);
    }
    check(p.kind == SchedulePicture::Kind::Begin, "a slot past the reference begins the waiting sequence");
    (void)schedule.supersede();
    schedule.begin_waiting(p.slot);
    check(!schedule.waiting() && schedule.sequence().frame_qpc != first, "the waiting sequence plays");
    const ScheduleCounts counts = schedule.take_counts();
    check(counts.started == 2u, "two sequences started");
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
        arrive(schedule, now, now, 0, true);
        generated = schedule.sequence().generated;
    }
    check(generated == 0u, "over budget: no generated pictures");
    check(sequences > 60, "not before the warm second");
    display.gpu_ms = 2.0;
    int back = 0;
    for (; back < 400 && generated == 0u; ++back) {
        now += static_cast<std::int64_t>(kStep);
        arrive(schedule, now, now, 0, true);
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
        arrive(with_upscaler, now, now, 0, true);
    }
    check(upscaled.suspended > 0u, "temporal upscaling on generated pictures suspended when none fits");
}

// The cadence governor (fed by each step's present request): game frames late by the render thread's generated
// pictures take them away; late by the game's own work (no waits on the render thread), a probe takes them away for a
// second and gives them back.
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
    arrive(schedule, kFrequency, kFrequency, 0, true);
    ScheduleWalk w = schedule.walk_time(&a, nullptr, false, 0.5f, true);
    check(w.reset, "nothing walked before: a new history");
    schedule.walked(&a, 0.5f);
    w = schedule.walk_time(&a, nullptr, false, 1.0f, true);
    check(!w.reset && w.t_prev == 0.5f, "the same pair: motion from t = 1/2");
    schedule.walked(&a, 1.0f);
    arrive(schedule, kFrequency * 2, kFrequency * 2, 0, true);
    w = schedule.walk_time(&b, &a, false, 0.5f, true);
    check(!w.reset && w.t_prev == 0.0f, "the next pair from its frame before's end: t = 0");
    w = schedule.walk_time(&b, &a, true, 0.5f, true);
    check(w.reset, "a camera cut: a new history");
    schedule.walked(&b, 0.75f);
    arrive(schedule, kFrequency * 3, kFrequency * 3, 0, true);
    w = schedule.walk_time(&c, &b, false, 0.25f, true);
    check(!w.reset && w.t_prev == -0.25f, "a pair replaced at t = 3/4: motion from t - 1 on the next");
    w = schedule.walk_time(&d, &a, false, 0.25f, true);
    check(w.reset, "skipped lists: a new history");
    const ScheduleCounts counts = schedule.take_counts();
    check(counts.history_carried == 1u && counts.history_resets == 3u, "history carried and reset counted");
    schedule.forget();
    check(schedule.walk_time(&c, &b, false, 0.5f, false).reset, "a lost device forgets the pairing");
    check(!schedule.busy(), "a lost device forgets the sequences");
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
        arrive(schedule, now, now, 0, true);
    }
    check(schedule.gpu_cap() == 0u, "capped");
    schedule.configure(1u, now);
    check(schedule.gpu_cap() == 0u && display.resets == 1u, "the same configuration changes nothing");
    schedule.configure(3u, now);
    check(schedule.gpu_cap() == kUnboundedGeneratedFrames && display.resets == 2u, "a new one starts over");
    schedule.configure(0u, now);
    check(schedule.slot_period() == 0 && !schedule.unbounded(), "none: no slot clock");
    arrive(schedule, now, now, 0, true, true);
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
    test_starts_at_arrival();
    test_varying_lateness();
    test_every_rate();
    test_variable_refresh();
    test_arrival_clock();
    test_unlimited();
    test_stall();
    test_waiting();
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
