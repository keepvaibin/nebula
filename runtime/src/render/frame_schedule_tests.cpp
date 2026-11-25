// SPDX-License-Identifier: GPL-3.0-only
// Tests of the frame schedule (frame_schedule.h, I-016) on simulated displays: the game's 60 steps a second shown at
// present rates that are and are not multiples of 60, with the display's vblanks timed and not, an unlimited rate and a
// variable refresh. The motion is even when every picture shows the game's time a constant delay before its slot
// (content time linear in present time); the slots of a timed display are its vblanks. A sequence starts when its list
// arrives, its reference its own step's moment plus the step clock's delay (I-004), and a slot before that reference
// shows the sequence before at its own t, never the frame before early (N-1084). The governors, a stall and the walk's
// history pairing are tested on their own.
#include "galaxy/render/frame_schedule.h"

#include "galaxy/frame_phase.h"
#include "galaxy/game_step.h"
#include "galaxy/precise_wait.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/variable_refresh.h"
#include "galaxy/step_lock.h"
#include "galaxy/vsync_grid.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <utility>
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
    bool variable = false;         // the refreshes follow the presents: the pacer's clock, at most `cap` hertz
    double cap = 0.0;
    std::int64_t vblank0 = 12'345;
    std::int64_t period = kFrequency / 120;
    double gpu_ms = 0.0;
    bool temporal = false;
    bool bench = false;
    unsigned suspended = 0;
    unsigned resets = 0;
    DisplayModel* model = nullptr;  // the display model's answers (G2.3.1); none: the platform gives none

    ScheduleSettings settings() const override { return {rate, vsync_on}; }
    bool display_time(std::int64_t present, std::int64_t& shown) const override {
        shown = 0;
        return model != nullptr && model->display_time(present, shown);
    }
    bool last_shown(ShownPicture& picture) override { return model != nullptr && model->take_shown(picture); }
    bool vblank_grid(std::int64_t& vblank, std::int64_t& grid) const override {
        if (!timed) return false;
        vblank = vblank0;
        grid = period;
        return true;
    }
    bool variable_refresh(double& held) const override {
        held = variable ? cap : 0.0;
        return variable;
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
    std::vector<std::int64_t> displayed;   // with a swap chain: the refresh that showed each picture of `shown`
    std::uint64_t superseded = 0;
};

// A flip-model swap chain on a SimDisplay's vblanks, as Direct3D's with frame latency 1 (renderer_d3d12.cpp
// present_to_swap_chain): a picture ready at `ready` is presented once the compositor took the one before it (the frame
// latency object's wait) and not before the model's present_from for its slot (the renderer's hold), and shown at the
// first refresh after its present and `held` refreshes more, never with the one before. Each present's display reaches
// the model at a later present, as the frame statistics read after a present give the newest present shown by then.
// With `asks`, a swap chain that takes each present's time, as Vulkan's present timing (renderer_vk.cpp, G2.3.2), on the
// PC (held 0) and on Android's compositor (held 1): a picture is presented when ready, with no wait and no hold of the
// renderer's, and shown no sooner than the refresh at or after half a refresh before its slot.
struct SimSwapChain {
    const SimDisplay& display;
    DisplayModel model{kFrequency};
    std::int64_t held = 0;
    bool asks = false;
    std::uint64_t id = 0;
    std::int64_t last_shown = 0;
    std::vector<std::pair<std::uint64_t, std::int64_t>> pending;  // presents not yet reported: id, shown

    // Presents a picture drawn for `slot` (0: none), ready at `ready`: the moment the present call returns.
    std::int64_t present(std::int64_t slot, std::int64_t ready) {
        const std::int64_t period = display.period;
        const std::int64_t call =
            asks ? ready : std::max({ready, last_shown - held * period, model.present_from(slot)});
        const std::int64_t first = display.vblank0 + ((call - display.vblank0) / period + 1) * period;
        std::int64_t shown = std::max(first + held * period, last_shown + period);
        if (asks && slot != 0) {
            const std::int64_t from = slot - period / 2 - display.vblank0;
            const std::int64_t asked =
                display.vblank0 + (from >= 0 ? (from + period - 1) / period : -(-from / period)) * period;
            shown = std::max(shown, asked);
        }
        std::size_t newest = pending.size();
        for (std::size_t i = 0; i < pending.size(); ++i) {
            if (pending[i].second <= call) newest = i;
        }
        if (newest != pending.size()) {
            model.displayed(pending[newest].first, pending[newest].second, period);
            pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(newest) + 1);
        }
        model.presented(++id, slot, ready, call, asks);
        pending.emplace_back(id, shown);
        last_shown = shown;
        return call;
    }
};

Run run(FrameSchedule& schedule, int frames, std::int64_t first_step, const std::vector<std::int64_t>& late,
        std::int64_t cost = kFrequency / 2000, bool with_step = true, std::uint64_t* waited = nullptr,
        double step_spacing = kStep, SimDisplay* display = nullptr, const std::vector<double>* rates = nullptr,
        SimSwapChain* chain = nullptr) {
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
        } else if (chain != nullptr) {
            now += cost;  // the frame's own walk
        }
        if (chain != nullptr) {
            // The present: the render thread waits for the swap chain's queue.
            now = std::max(now, chain->present(picture.flush ? 0 : picture.slot, now));
            result.displayed.push_back(chain->last_shown);
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
// next step asks for put it a whole step and more later. At 144 Hz: on vblanks the step locks to (120 Hz) the reference
// moves on to the next vblank (G2.2, test_lead_at_multiples).
void test_starts_at_arrival() {
    SimDisplay display = display_at(144.0, true);
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(interpolated_frames_per_game_frame(144.0), 0);
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
// most the delay (under half a step) before its slot. G2.2 proof 1: a sequence has a lead only at a whole multiple of
// 60 (1/k): on timed vblanks where the step locks to them (exact, as galaxy/step_lock.h with in-between pictures on),
// on a swap chain that tears within 0.2 %; at every other rate the lead is 0.
double expected_lead(double refresh, bool timed) {
    const double k = std::round(refresh / kGameStepRate);
    const double off = std::abs(refresh - k * kGameStepRate);
    if (k < 2.0) return 0.0;
    return off * (timed ? 5000.0 : 500.0) <= k * kGameStepRate ? 1.0 / k : 0.0;
}

// The head's sample rates: the named ones and 300 random ones between 40 and 500 Hz from a fixed seed.
std::vector<double> sample_rates() {
    std::vector<double> rates{40.0, 48.0, 50.0, 59.94, 60.0, 72.0, 75.0, 82.37, 90.0, 100.0, 119.88, 120.0,
                              144.0, 165.0, 180.0, 200.0, 240.0, 360.0, 480.0, 500.0};
    std::mt19937 random(20261124u);
    std::uniform_real_distribution<double> any(40.0, 500.0);
    for (int i = 0; i < 300; ++i) rates.push_back(any(random));
    return rates;
}

void test_every_rate() {
    const std::vector<double> rates = sample_rates();
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
                const double lead = interpolated ? expected_lead(refresh, timed) : 0.0;
                bool leads = true;
                for (const Shown& s : r.shown) leads &= std::abs(s.lead - lead) < 1e-9;
                const bool ok = e.early == 0u && e.frames_once && (!interpolated || (e.rising && e.on_grid)) &&
                                (!interpolated || l == 2 || e.lag_max < kStep / 2.0) && leads;
                if (!ok && bad++ < 10u) {
                    std::printf("  %.2f Hz %s %s: early %zu, frames once %d, rising %d, on grid %d, lag %.0f ticks, "
                                "lead %s\n",
                                refresh, timed ? "timed" : "tearing", load_names[l], e.early, e.frames_once ? 1 : 0,
                                e.rising ? 1 : 0, e.on_grid ? 1 : 0, e.lag_max, leads ? "right" : "WRONG");
                }
            }
        }
    }
    char what[120];
    std::snprintf(what, sizeof what, "every rate, timed and tearing, three loads: %u of %zu runs wrong", bad,
                  rates.size() * 6u);
    check(bad == 0u, what);
}

// What each list of a run's settled part showed (G2.2): its frame shown on a slot at t = 1 (the real picture), how many
// pictures it took (walks), and its sequence's lead.
struct ListShows {
    std::size_t lists = 0;
    std::size_t real = 0;         // lists whose frame was shown at t = 1 on a slot
    std::size_t walks_wrong = 0;  // lists that took other than `walks` pictures
    std::size_t lead_wrong = 0;   // pictures whose sequence's lead is not `lead`
    std::size_t off_spacing = 0;  // a list's slots not `spacing` apart (0: not checked)
    bool on_grid = true;          // every slot a vblank of `grid`
};

ListShows list_shows(const Run& r, std::size_t from, std::size_t to, std::size_t walks, double lead,
                     std::int64_t spacing, const SimDisplay* grid) {
    ListShows l;
    for (std::size_t list = from; list < to; ++list) {
        ++l.lists;
        std::size_t pictures = 0;
        bool real = false;
        std::int64_t last_slot = 0;
        for (const Shown& s : r.shown) {
            if (s.frame != static_cast<int>(list)) continue;
            ++pictures;
            if (std::abs(s.lead - lead) > 1e-9) ++l.lead_wrong;
            if (grid != nullptr && (s.picture.slot - grid->vblank0) % grid->period != 0) l.on_grid = false;
            if (spacing != 0 && last_slot != 0 && s.picture.slot - last_slot != spacing) ++l.off_spacing;
            last_slot = s.picture.slot;
            real |= s.picture.kind == SchedulePicture::Kind::Frame && !s.picture.flush &&
                    std::abs(s.picture.t - 1.0) < 1e-3;
        }
        l.real += real ? 1u : 0u;
        l.walks_wrong += pictures != walks ? 1u : 0u;
    }
    return l;
}

// G2.2 proofs 1 and 2 (I-002): at 120, 180, 240, 360 and 480 Hz on timed vblanks, the step locked to them and started
// its work's budget (its longest work and 1.5 ms) before a vblank, as the host's frame phase does: each sequence has
// lead 1/k, its last slot is a vblank that shows its frame at t = 1, and each step takes k walks, none unseen (before:
// lead 0, 5 to 13 real pictures in 2,640 steps, k + 1 walks a step). Steady and varying lateness.
void test_lead_at_multiples() {
    std::vector<std::int64_t> steady{kFrequency / 500};
    std::vector<std::int64_t> varying;
    for (int i = 0; i < 64; ++i) varying.push_back(kFrequency / 1000 + (i * 29 % 64) * (kFrequency / 12'800));  // 1-6 ms
    for (const double refresh : {120.0, 180.0, 240.0, 360.0, 480.0}) {
        for (const std::vector<std::int64_t>* late : {&steady, &varying}) {
            SimDisplay display = display_at(refresh, true);
            FrameSchedule schedule(display, kFrequency);
            const unsigned generated = interpolated_frames_per_game_frame(refresh);
            schedule.configure(generated, 0);
            const std::size_t k = generated + 1u;
            const std::int64_t budget = *std::max_element(late->begin(), late->end()) + kFrequency * 15 / 10'000;
            const std::int64_t first = display.vblank0 + 600 * display.period - budget;
            const Run r = run(schedule, 600, first, *late, kFrequency / 2000, true, nullptr,
                              static_cast<double>(static_cast<std::int64_t>(k) * display.period));
            const ListShows l = list_shows(r, 300, 590, k, 1.0 / static_cast<double>(k), 0, &display);
            char what[200];
            std::snprintf(what, sizeof what,
                          "%.0f Hz locked, %s lateness: real pictures %zu of %zu lists, walks other than %zu: %zu, "
                          "lead not 1/%zu: %zu, on vblanks %d",
                          refresh, late == &steady ? "steady" : "1-6 ms", l.real, l.lists, k, l.walks_wrong, k,
                          l.lead_wrong, l.on_grid ? 1 : 0);
            check(l.real == l.lists && l.walks_wrong == 0u && l.lead_wrong == 0u && l.on_grid, what);
            // Each reference moved to the first vblank at or after its step's moment plus the delay (within the delay
            // estimate's 0.1 ms step).
            const std::int64_t delay = schedule.delay_qpc();
            std::size_t moved_wrong = 0;
            for (std::size_t i = 300; i < 590 && i < r.references.size(); ++i) {
                const std::int64_t after = r.references[i] - (r.steps[i] + delay);
                moved_wrong += (r.references[i] - display.vblank0) % display.period != 0 ||
                                       after < -kFrequency / 10'000 || after >= display.period + kFrequency / 10'000
                                   ? 1u
                                   : 0u;
            }
            std::snprintf(what, sizeof what, "%.0f Hz locked, %s lateness: references not on the next vblank: %zu",
                          refresh, late == &steady ? "steady" : "1-6 ms", moved_wrong);
            check(moved_wrong == 0u, what);
        }
    }
    // The step not locked (59.94 Hz's multiple, 119.88 Hz, is 0.1 % off: in-between pictures keep the step at exactly
    // 60): lead 0.
    SimDisplay display = display_at(119.88, true);
    FrameSchedule schedule(display, kFrequency);
    schedule.configure(interpolated_frames_per_game_frame(119.88), 0);
    const Run r = run(schedule, 300, kFrequency, steady);
    bool none = true;
    for (const Shown& s : r.shown) none &= s.lead == 0.0;
    check(none, "119.88 Hz timed: the step is not locked, no lead");
}

// G2.2 proof 5: a variable refresh display, whose slot clock is the pacer's own (its vblanks, here timed at 144 Hz, are
// never the clock). At a limit of 60, 120 or 180 under a cap of 224, and 240 under a cap of 240, each sequence has
// lead 1/k and its last slot shows its frame at t = 1, the slots 1/limit apart from the reference; at 60 every picture
// is a real one. At 90, 100, 144 and 82.37, and at 240 above a cap of 224 (the cap's 224 then), the lead is 0.
void test_variable_clock() {
    const std::vector<std::int64_t> steady{kFrequency / 500};
    struct Case {
        double limit;
        double cap;
        std::size_t k;  // 0: no lead
    };
    const Case cases[] = {{60.0, 224.0, 1}, {120.0, 224.0, 2}, {180.0, 224.0, 3}, {240.0, 240.0, 4},
                          {90.0, 224.0, 0}, {100.0, 224.0, 0}, {144.0, 224.0, 0}, {82.37, 224.0, 0},
                          {240.0, 224.0, 0}};
    for (const Case& c : cases) {
        SimDisplay display = display_at(144.0, true);
        display.variable = true;
        display.cap = c.cap;
        display.rate = c.limit;
        FrameSchedule schedule(display, kFrequency);
        const double rate = std::min(c.limit, c.cap);
        schedule.configure(interpolated_frames_per_game_frame(rate), 0);
        const Run r = run(schedule, 300, kFrequency, steady);
        char what[200];
        if (c.k == 1u) {
            bool real = !r.shown.empty();
            for (const Shown& s : r.shown) real &= s.picture.kind == SchedulePicture::Kind::Frame;
            std::snprintf(what, sizeof what, "variable refresh, limit 60: every picture a real one");
            check(real, what);
            continue;
        }
        if (c.k == 0u) {
            bool none = true;
            for (const Shown& s : r.shown) none &= s.lead == 0.0;
            std::snprintf(what, sizeof what, "variable refresh, limit %.2f under a cap of %.0f: no lead", c.limit, c.cap);
            check(none, what);
            continue;
        }
        const auto spacing = static_cast<std::int64_t>(static_cast<double>(kFrequency) / c.limit);
        const ListShows l = list_shows(r, 150, 290, c.k, 1.0 / static_cast<double>(c.k), spacing, nullptr);
        std::snprintf(what, sizeof what,
                      "variable refresh, limit %.0f under a cap of %.0f: real pictures %zu of %zu lists, walks other "
                      "than %zu: %zu, lead wrong %zu, slots not 1/limit apart %zu",
                      c.limit, c.cap, l.real, l.lists, c.k, l.walks_wrong, l.lead_wrong, l.off_spacing);
        check(l.real == l.lists && l.walks_wrong == 0u && l.lead_wrong == 0u && l.off_spacing == 0u, what);
    }
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

// G2.3.1 proof 1: the display model, Direct3D's (SimSwapChain: frame latency 1, the frame statistics read after each
// present). At every sample rate of the head that has in-between pictures, with the compositor holding pictures 0 and
// 1 refreshes, steady lateness: once the lead has settled, each picture is shown at the refresh it was drawn for (its
// slot, whose t it shows), every slot a vblank, one picture a refresh, none early, no frame twice. The lead comes down
// to the least the display allows (held + 1 refreshes) where a picture's margin leaves a refresh and 2 ms to spare
// (refreshes of 3.1 ms and longer, with a walk of 0.5 ms and the hold's guard of 0.5 ms, which the render thread waits
// out before the next picture), and is never more than a refresh above it. A compositor that
// starts holding a refresh more raises the lead from what was shown, and the pictures reach their slots again. A
// variable refresh (the pacer's clock) and a display with no answers keep lead 0: today's slots.
void test_display_model() {
    const std::vector<std::int64_t> steady{kFrequency / 500};
    const std::int64_t walk = kFrequency / 2000;
    const auto on_slots = [](const Run& r, std::size_t from) {
        std::size_t off = 0;
        for (std::size_t i = from; i < r.shown.size(); ++i) {
            off += !r.shown[i].picture.flush && r.displayed[i] != r.shown[i].picture.slot ? 1u : 0u;
        }
        return off;
    };
    unsigned bad = 0;
    std::size_t runs = 0;
    for (const double refresh : sample_rates()) {
        const unsigned generated = interpolated_frames_per_game_frame(refresh);
        if (generated == 0u) continue;  // no slots: each picture a game frame, shown at once
        for (const std::int64_t held : {std::int64_t{0}, std::int64_t{1}}) {
            ++runs;
            SimDisplay display = display_at(refresh, true);
            SimSwapChain chain{display};
            chain.held = held;
            display.model = &chain.model;
            FrameSchedule schedule(display, kFrequency);
            schedule.configure(generated, 0);
            const Run r = run(schedule, 900, kFrequency, steady, walk, true, nullptr, kStep, &display, nullptr, &chain);
            const std::size_t from = r.shown.size() * 2 / 3;
            const std::size_t off = on_slots(r, from);
            const Evenness e = evenness(r, from, display.period, &display);
            const std::int64_t lead = schedule.display_lead();
            const std::int64_t least = (held + 1) * display.period;
            const bool lead_ok = display.period >= kFrequency * 31 / 10'000 ? lead == least
                                 : display.period <= kFrequency * 29 / 10'000
                                     ? lead == least + display.period
                                     : lead == least || lead == least + display.period;
            const bool ok = off == 0u && e.early == 0u && e.frames_once && e.rising && e.on_grid && lead_ok;
            if (!ok && bad++ < 10u) {
                std::printf("  %.2f Hz held %lld: off their slots %zu of %zu, early %zu, frames once %d, rising %d, "
                            "on grid %d, lead %lld ticks (least %lld)\n",
                            refresh, static_cast<long long>(held), off, r.shown.size() - from, e.early,
                            e.frames_once ? 1 : 0, e.rising ? 1 : 0, e.on_grid ? 1 : 0, static_cast<long long>(lead),
                            static_cast<long long>(least));
            }
        }
    }
    char what[160];
    std::snprintf(what, sizeof what, "the display model at every sample rate, held 0 and 1: %u of %zu runs wrong", bad, runs);
    check(runs > 500u && bad == 0u, what);

    // 240 Hz: the compositor starts holding a refresh more after 15 s.
    {
        SimDisplay display = display_at(240.0, true);
        SimSwapChain chain{display};
        display.model = &chain.model;
        FrameSchedule schedule(display, kFrequency);
        schedule.configure(interpolated_frames_per_game_frame(240.0), 0);
        (void)run(schedule, 900, kFrequency, steady, walk, true, nullptr, kStep, &display, nullptr, &chain);
        const std::int64_t before = schedule.display_lead();
        chain.held = 1;
        const Run r = run(schedule, 900, kFrequency + static_cast<std::int64_t>(std::llround(900 * kStep)), steady, walk,
                          true, nullptr, kStep, &display, nullptr, &chain);
        const std::size_t off = on_slots(r, r.shown.size() * 2 / 3);
        std::snprintf(what, sizeof what,
                      "240 Hz, the compositor holding a refresh more: lead %lld then %lld ticks, %zu pictures off their "
                      "slots",
                      static_cast<long long>(before), static_cast<long long>(schedule.display_lead()), off);
        check(before == display.period && schedule.display_lead() == 2 * display.period && off == 0u, what);
    }
    // No refresh to ask for under a variable refresh, no answers from a display without a model: lead 0.
    {
        SimDisplay display = display_at(144.0, true);
        display.variable = true;
        display.cap = 144.0;
        SimSwapChain chain{display};
        display.model = &chain.model;
        FrameSchedule schedule(display, kFrequency);
        schedule.configure(interpolated_frames_per_game_frame(144.0), 0);
        (void)run(schedule, 300, kFrequency, steady, walk, true, nullptr, kStep, &display, nullptr, &chain);
        check(schedule.display_lead() == 0 && schedule.display_delay_qpc() == 0,
              "a variable refresh: the pacer's clock, no display lead");
    }
    {
        SimDisplay display = display_at(144.0, true);
        FrameSchedule schedule(display, kFrequency);
        schedule.configure(interpolated_frames_per_game_frame(144.0), 0);
        (void)run(schedule, 300, kFrequency, steady);
        check(schedule.display_lead() == 0 && schedule.display_delay_qpc() == 0,
              "a display with no answers: no display lead, today's slots");
    }
}

// The Vulkan and Android answers (G2.3.2): a swap chain that takes each present's time (present timing) holds a picture
// to the refresh it asks for, with no wait or hold of the renderer's. At every sample rate, the display holding 0 (the
// PC), 1 and 2 refreshes (Android's compositor), each picture is shown at its slot, which is the refresh its t was drawn
// for, and the lead comes down to the least the display allows.
void test_display_model_asked() {
    const std::vector<std::int64_t> steady{kFrequency / 500};
    const std::int64_t walk = kFrequency / 2000;
    const auto off_slots = [](const Run& r, std::size_t from) {
        std::size_t off = 0;
        for (std::size_t i = from; i < r.shown.size(); ++i) {
            off += !r.shown[i].picture.flush && r.displayed[i] != r.shown[i].picture.slot ? 1u : 0u;
        }
        return off;
    };
    unsigned bad = 0;
    std::size_t runs = 0;
    for (const double refresh : sample_rates()) {
        const unsigned generated = interpolated_frames_per_game_frame(refresh);
        if (generated == 0u) continue;  // no slots: each picture a game frame, shown at once
        for (const std::int64_t held : {std::int64_t{0}, std::int64_t{1}, std::int64_t{2}}) {
            ++runs;
            SimDisplay display = display_at(refresh, true);
            SimSwapChain chain{display};
            chain.held = held;
            chain.asks = true;
            display.model = &chain.model;
            FrameSchedule schedule(display, kFrequency);
            schedule.configure(generated, 0);
            const Run r = run(schedule, 900, kFrequency, steady, walk, true, nullptr, kStep, &display, nullptr, &chain);
            const std::size_t from = r.shown.size() * 2 / 3;
            const std::size_t off = off_slots(r, from);
            const Evenness e = evenness(r, from, display.period, &display);
            const std::int64_t lead = schedule.display_lead();
            const std::int64_t least = (held + 1) * display.period;
            // A calm stretch steps the lead down while a picture is ready a refresh and 2 ms before its last moment: one
            // refresh above the least does so where a refresh is 2.5 ms or more (the walk takes 0.5 ms).
            const bool lead_ok = display.period >= kFrequency * 26 / 10'000 ? lead == least
                                 : display.period <= kFrequency * 24 / 10'000
                                     ? lead == least + display.period
                                     : lead == least || lead == least + display.period;
            const bool ok = off == 0u && e.early == 0u && e.frames_once && e.rising && e.on_grid && lead_ok &&
                            chain.model.held() == held;
            if (!ok && bad++ < 10u) {
                std::printf("  asked, %.2f Hz held %lld: off their slots %zu of %zu, early %zu, frames once %d, rising "
                            "%d, on grid %d, lead %lld ticks (least %lld), model held %lld\n",
                            refresh, static_cast<long long>(held), off, r.shown.size() - from, e.early,
                            e.frames_once ? 1 : 0, e.rising ? 1 : 0, e.on_grid ? 1 : 0, static_cast<long long>(lead),
                            static_cast<long long>(least), static_cast<long long>(chain.model.held()));
            }
        }
    }
    char what[180];
    std::snprintf(what, sizeof what,
                  "the asked times at every sample rate, held 0, 1 and 2: %u of %zu runs wrong", bad, runs);
    check(runs > 700u && bad == 0u, what);

    // 240 Hz: the display starts holding a refresh more after 15 s; the late pictures show it and the lead follows.
    {
        SimDisplay display = display_at(240.0, true);
        SimSwapChain chain{display};
        chain.asks = true;
        display.model = &chain.model;
        FrameSchedule schedule(display, kFrequency);
        schedule.configure(interpolated_frames_per_game_frame(240.0), 0);
        (void)run(schedule, 900, kFrequency, steady, walk, true, nullptr, kStep, &display, nullptr, &chain);
        const std::int64_t before = schedule.display_lead();
        chain.held = 1;
        const Run r = run(schedule, 900, kFrequency + static_cast<std::int64_t>(std::llround(900 * kStep)), steady, walk,
                          true, nullptr, kStep, &display, nullptr, &chain);
        const std::size_t off = off_slots(r, r.shown.size() * 2 / 3);
        std::snprintf(what, sizeof what,
                      "240 Hz asked, the display holding a refresh more: lead %lld then %lld ticks, %zu pictures off "
                      "their slots",
                      static_cast<long long>(before), static_cast<long long>(schedule.display_lead()), off);
        check(before == display.period && schedule.display_lead() == 2 * display.period && off == 0u, what);
    }
}

// The model alone: a present's held refreshes, the least over a window of a second, the answers' end a second after
// the last report, a new swap chain starting it again.
void test_display_model_answers() {
    const std::int64_t period = kFrequency / 120;
    const std::int64_t v = 1'000'000;  // a refresh
    DisplayModel model(kFrequency);
    std::int64_t shown = 0;
    check(!model.display_time(v, shown), "the model gives no answer before a report");
    model.presented(1u, v + period, v + period / 4, v + period / 2);
    model.displayed(1u, v + 2 * period, period);  // shown a refresh past the first after its call
    check(model.held() == 1, "a present held a refresh past the first after its call: held 1");
    check(model.display_time(v + 3 * period + 1, shown) && shown == v + 5 * period,
          "a present after a refresh: the next refresh and one held");
    ShownPicture picture;
    check(model.take_shown(picture) && picture.slot == v + period && picture.shown == v + 2 * period &&
              picture.earliest == v + 2 * period && picture.margin == period - period / 4 - period,
          "the picture shown: its slot, the earliest refresh from its ready moment, its margin");
    check(!model.take_shown(picture), "each picture shown is taken once");
    // A present held 0 within the window: the least comes in at the window's end.
    model.presented(2u, 0, v + 2 * period + 10, v + 2 * period + 10);
    model.displayed(2u, v + 3 * period, period);
    check(model.held() == 1 && !model.take_shown(picture), "within the window the held stays; no slot, nothing taken");
    model.presented(3u, 0, v + kFrequency, v + kFrequency);
    model.displayed(3u, v + kFrequency + 2 * period, period);
    check(model.held() == 0, "at the window's end the least of its presents");
    check(!model.display_time(v + 2 * kFrequency + 3 * period, shown), "no answer a second after the last report");
    model.presented(1u, 0, v, v);
    check(!model.display_time(v, shown), "a new swap chain (its count from the start): no answer until a report");

    // Presents that asked their time (G2.3.2). One shown a refresh later than asked: the display's own hold, 1.
    const std::int64_t f = kFrequency;
    DisplayModel asked(f);
    asked.presented(1u, v + period, v, v + 10, true);
    asked.displayed(1u, v + 2 * period, period);
    check(asked.held() == 1, "an asked picture shown later than asked: the hold it shows is the display's");
    // Asked three refreshes past the model's and shown there: held by its own time, no sample. The window that ends
    // keeps the sample it had (1); the next ends with none and keeps the last.
    asked.presented(2u, v + f + 4 * period, v + f + 10, v + f + 10, true);
    asked.displayed(2u, v + f + 4 * period, period);
    asked.presented(3u, v + 2 * f + 4 * period, v + 2 * f + 10, v + 2 * f + 10, true);
    asked.displayed(3u, v + 2 * f + 4 * period, period);
    check(asked.held() == 1 && asked.display_time(v + 2 * f + 4 * period + 1, shown),
          "pictures held to a later asked refresh say nothing of the hold: a window with none keeps it, answers go on");
    check(asked.take_shown(picture) && picture.slot == v + 2 * f + 4 * period &&
              picture.shown == v + 2 * f + 4 * period,
          "an asked picture shown at its slot is reported as shown there");
    // Shown two refreshes later than asked: the display holds 2 now; at the window's end the least of the window.
    asked.presented(4u, v + 2 * f + 5 * period, v + 2 * f + 4 * period + 10, v + 2 * f + 4 * period + 10, true);
    asked.displayed(4u, v + 2 * f + 7 * period, period);
    check(asked.held() == 1, "within the window the held stays");
    asked.presented(5u, v + 3 * f + 5 * period, v + 3 * f + 4 * period + 10, v + 3 * f + 4 * period + 10, true);
    asked.displayed(5u, v + 3 * f + 7 * period, period);
    check(asked.held() == 2, "pictures shown later than asked show the display's hold, which the model takes");
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

// G2.3.5: variable refresh, told by the system or proved from the presents (render/variable_refresh.h).

// Presents held `held` ticks apart from `from` on a display of `mode_hz`, each shown at the first refresh after its call
// (fixed: the vblanks every mode period from `vblank0`) or 1 ms after its call (variable: the refresh follows the present),
// each moment off by up to `jitter` ticks as a stamp is; the question hears of each (its id, call, held period, shown).
struct ProofPresents {
    double mode_hz = 144.0;
    bool variable = false;
    std::int64_t held = 0;
    std::int64_t jitter = kFrequency / 20000;  // 50 us
    std::int64_t vblank0 = 777;
    std::uint64_t id = 0;
    std::int64_t now = kFrequency;
    std::mt19937 random{20261124u};

    [[nodiscard]] std::int64_t mode() const { return std::llround(static_cast<double>(kFrequency) / mode_hz); }
    void present(RefreshQuestion& question, int count, std::int64_t spacing = 0) {
        std::uniform_int_distribution<std::int64_t> noise(-jitter, jitter);
        for (int i = 0; i < count; ++i) {
            now += spacing != 0 ? spacing : held;
            const std::int64_t period = mode();
            const std::int64_t shown =
                variable ? now + kFrequency / 1000 : vblank0 + ((now - vblank0) / period + 1) * period;
            question.presented(++id, now, held);
            question.shown(id, shown + noise(random));
        }
    }
};

// Proof 1, point 5 (Windows' frame statistics) and point 7 (Vulkan's present-completion stamps): the decision as a unit at
// the head's sample rates. A fixed display (each picture on whole refreshes: a composed window is one too) answers fixed,
// a variable one variable, after 120 counted presents at the probe rate. A machine with no tearing support answers fixed
// whatever the presents show. Each of the five events asks again: (a) to (d) through ask (the hosts' g_display_recheck,
// the renderers' monitor_changed), (e) when 120 presents counted no longer fit the answer held.
void test_refresh_proof() {
    for (const double mode_hz : {60.0, 75.0, 82.37, 100.0, 120.0, 144.0, 165.0, 240.0, 360.0}) {
        for (const bool variable : {false, true}) {
            RefreshQuestion question;
            ProofPresents presents;
            presents.mode_hz = mode_hz;
            presents.variable = variable;
            question.ask(presents.mode());
            presents.held = presents.mode() + presents.mode() / 4;
            char what[160];
            std::snprintf(what, sizeof what, "%.2f Hz %s: probing before an answer", mode_hz, variable ? "variable" : "fixed");
            check(question.probing(true, true) && !question.probing(true, false) && !question.probing(false, true), what);
            presents.present(question, 150);
            const RefreshKind want = variable ? RefreshKind::kVariable : RefreshKind::kFixed;
            std::snprintf(what, sizeof what, "%.2f Hz %s: the presents prove it", mode_hz, variable ? "variable" : "fixed");
            check(question.proved() == want && !question.probing(true, true), what);
            std::snprintf(what, sizeof what, "%.2f Hz %s: no tearing support answers fixed", mode_hz,
                          variable ? "variable" : "fixed");
            check(question.varies(true) == variable && !question.varies(false), what);
            // (e): the other kind's presents, counted at the probe rate, ask again; then they prove the other kind.
            presents.variable = !variable;
            const RefreshKind other = variable ? RefreshKind::kFixed : RefreshKind::kVariable;
            bool asked_again = false;
            for (int i = 0; i < 480 && question.proved() != other; ++i) {
                presents.present(question, 1);
                asked_again |= question.proved() == RefreshKind::kUnknown && question.probing(true, true);
            }
            std::snprintf(what, sizeof what, "%.2f Hz %s: statistics that no longer fit ask again, then prove the other",
                          mode_hz, variable ? "variable" : "fixed");
            check(asked_again && question.proved() == other, what);
            // (a) to (d): ask.
            question.ask(presents.mode());
            std::snprintf(what, sizeof what, "%.2f Hz: an event asks again", mode_hz);
            check(question.proved() == RefreshKind::kUnknown && question.probing(true, true), what);
        }
    }
    // A composed window on a variable display: the compositor shows each picture at its own fixed refresh.
    {
        RefreshQuestion question;
        ProofPresents presents;
        presents.mode_hz = 165.0;
        question.ask(presents.mode());
        presents.held = presents.mode() + presents.mode() / 4;
        presents.present(question, 150);
        check(question.proved() == RefreshKind::kFixed && !question.varies(true), "a composed window answers fixed");
    }
    // Shown moments with the stamp's jitter at 240 Hz and twice that: a quarter of a period of noise answers nothing
    // wrong (no present fits one kind and not the other, so none is counted).
    {
        RefreshQuestion question;
        ProofPresents presents;
        presents.mode_hz = 240.0;
        presents.jitter = presents.mode() / 4;
        question.ask(presents.mode());
        presents.held = presents.mode() + presents.mode() / 4;
        presents.present(question, 600);
        check(question.proved() != RefreshKind::kVariable, "a fixed display under heavy stamp noise never answers variable");
    }
    // A window whose presents are never reported (hidden, a remote session): the probe stops after its presents, no
    // answer (fixed), until asked again.
    {
        RefreshQuestion question;
        const std::int64_t mode = std::llround(kFrequency / 144.0);
        question.ask(mode);
        for (int i = 1; i < RefreshQuestion::kProbePresents; ++i) question.presented(static_cast<std::uint64_t>(i), i * mode, mode);
        const bool still = question.probing(true, true);
        question.presented(static_cast<std::uint64_t>(RefreshQuestion::kProbePresents), RefreshQuestion::kProbePresents * mode, mode);
        const bool stopped = !question.probing(true, true) && !question.varies(true);
        question.ask(mode);
        check(still && stopped && question.probing(true, true), "an unreported window stops probing after its presents");
    }
    // Point 7: what Vulkan's present timing says outright, and the system's word over the presents'.
    check(vulkan_refresh_kind(std::numeric_limits<std::uint64_t>::max(), 4166667u) == RefreshKind::kVariable,
          "refreshInterval UINT64_MAX: variable");
    check(vulkan_refresh_kind(4166667u, 4166667u) == RefreshKind::kFixed, "refreshInterval = refreshDuration: fixed");
    check(vulkan_refresh_kind(0u, 4166667u) == RefreshKind::kUnknown && vulkan_refresh_kind(8333333u, 4166667u) == RefreshKind::kUnknown,
          "refreshInterval 0 (or another value): the presents decide");
    check(refresh_varies(true, RefreshKind::kFixed, RefreshKind::kVariable) == false &&
              refresh_varies(true, RefreshKind::kVariable, RefreshKind::kFixed) &&
              refresh_varies(true, RefreshKind::kUnknown, RefreshKind::kVariable) &&
              !refresh_varies(true, RefreshKind::kUnknown, RefreshKind::kUnknown),
          "the system's answer over the presents', not known taken as fixed");
    {
        RefreshQuestion question;
        question.ask(std::llround(kFrequency / 144.0));
        question.told(RefreshKind::kVariable, 1.0 / 144.0);
        check(!question.probing(true, true) && question.varies(true), "a system that says it: no probe");
    }
    // Point 6: Android's hasArrSupport as it answers, never asked below API level 36.
    check(android_refresh_kind(36, true) == RefreshKind::kVariable && android_refresh_kind(37, true) == RefreshKind::kVariable,
          "Android 16 and up: hasArrSupport true is variable");
    check(android_refresh_kind(36, false) == RefreshKind::kFixed, "Android 16: hasArrSupport false is fixed");
    check(android_refresh_kind(35, true) == RefreshKind::kFixed && android_refresh_kind(31, true) == RefreshKind::kFixed,
          "below API 36 the answer is fixed, whatever was fed");
    // A Mac: a range of refresh intervals is variable.
    check(mac_refresh_kind(1.0 / 120.0, 1.0 / 24.0) == RefreshKind::kVariable &&
              mac_refresh_kind(1.0 / 60.0, 1.0 / 60.0) == RefreshKind::kFixed &&
              mac_refresh_kind(0.0, 0.0) == RefreshKind::kUnknown,
          "macOS: minimum and maximum refresh intervals");
}

// Proof 1, point 1: below the range. Presents slower than the display's longest interval (a machine too slow, a limit under
// the range) never count, so the answer held is not judged again from them; fed to the schedule with the answer variable
// (the pacer's clock at the cap), after 10 such pictures and after 1000 no slot is planned twice, no list's frame is shown
// twice and the lead stays 0, as inside the range.
void test_below_range() {
    for (const bool variable : {false, true}) {
        RefreshQuestion question;
        ProofPresents presents;
        presents.variable = variable;
        question.ask(presents.mode());
        presents.held = presents.mode() + presents.mode() / 4;
        presents.present(question, 150);
        const RefreshKind answer = question.proved();
        presents.variable = !variable;  // what slow presents show must not matter
        presents.held = 0;              // the pacer held none
        presents.present(question, 1000, kFrequency / 30);
        presents.held = kFrequency / 30;  // or held to a rate under half the mode's
        presents.present(question, 1000);
        check(question.proved() == answer, "presents below the range are never counted");
    }
    const std::vector<std::int64_t> steady{kFrequency / 500};
    for (const int slow : {10, 1000}) {
        SimDisplay display = display_at(144.0, false);
        display.vsync_on = true;
        display.variable = true;
        display.cap = 138.0;
        FrameSchedule schedule(display, kFrequency);
        schedule.configure(interpolated_frames_per_game_frame(138.0), 0);
        const Run inside = run(schedule, 300, kFrequency, steady);
        // Each picture 30 ms of the render thread: under 34 a second, below any range's lowest rate read (48 Hz).
        const Run below = run(schedule, slow, inside.steps.back() + static_cast<std::int64_t>(kStep), steady,
                              kFrequency * 3 / 100);
        const Evenness e = evenness(below, 0, 1, nullptr);
        bool lead = true;
        for (const Shown& s : below.shown) lead &= s.lead == 0.0;
        for (const Shown& s : inside.shown) lead &= s.lead == 0.0;
        char what[160];
        std::snprintf(what, sizeof what, "below the range, %d slow lists: slots rising %d, frames once %d, lead 0 %d", slow,
                      e.rising ? 1 : 0, e.frames_once ? 1 : 0, lead ? 1 : 0);
        check(e.rising && e.frames_once && lead && !below.shown.empty(), what);
    }
}

// Proof 1, point 2: the cap is one over (the shortest interval + the margin), the margin the 99.9th percentile of the
// precise wait's lateness, 1/3600 s until 1000 waits are in.
void test_refresh_cap() {
    check(std::abs(variable_refresh_cap_hz(1.0 / 240.0, kStartMarginSeconds) - 225.0) < 1e-9,
          "240 Hz with the start margin: 225 Hz");
    check(std::abs(variable_refresh_cap_hz(1.0 / 144.0, 0.0) - 144.0) < 1e-9 && variable_refresh_cap_hz(0.0, 0.001) == 0.0,
          "no margin: the shortest interval's rate; no interval: no cap");
    galaxy::WaitLateness lateness;
    for (int i = 0; i < 999; ++i) lateness.add(20);
    check(lateness.percentile_999_s(kStartMarginSeconds) == kStartMarginSeconds, "under 1000 waits: the start margin");
    lateness.add(3000);
    check(std::abs(lateness.percentile_999_s(kStartMarginSeconds) - 30e-6) < 1e-12,
          "999 waits 20 us late and one 3 ms: the 99.9th percentile is 30 us (its step's top)");
    for (int i = 0; i < 10; ++i) lateness.add(3000);
    check(lateness.percentile_999_s(kStartMarginSeconds) > 3e-3, "ten waits in 1010 3 ms late: the margin covers them");
    lateness.reset();
    check(lateness.count() == 0u && lateness.percentile_999_s(kStartMarginSeconds) == kStartMarginSeconds,
          "a display change measures again");
    const double cap = variable_refresh_cap_hz(1.0 / 144.0, 30e-6);
    check(cap < 144.0 && 1.0 / cap - 1.0 / 144.0 > 29e-6, "the cap's period is the shortest interval and the margin");
}

// Proof 1, point 4: under variable refresh the step is never locked to the display, at 60, 120, 180 and 240 Hz, in each
// of step_lock_refreshes' three callers (Windows' frame phase, Android's and Linux's grid, Apple's display link on the
// same grid); under a fixed display it locks.
void test_variable_step_unlocked() {
    constexpr std::int64_t kGameNs = 16666667;
    for (const int hz : {60, 120, 180, 240}) {
        const std::int64_t refresh = std::llround(1e9 / hz);
        char what[160];
        for (const bool in_between : {false, true}) {
            std::snprintf(what, sizeof what, "%d Hz: step_lock_refreshes 0 under variable, %d under fixed", hz, hz / 60);
            check(galaxy::step_lock_refreshes(kGameNs, refresh, in_between, true) == 0 &&
                      galaxy::step_lock_refreshes(kGameNs, refresh, in_between, false) == hz / 60,
                  what);
            galaxy::VsyncGrid grid;
            for (int i = 0; i < 240; ++i) grid.note(1000000000 + i * refresh, 0);
            std::int64_t slot = 0, frame_period = 0;
            const std::int64_t now = grid.vsync_ns + 1000;
            std::snprintf(what, sizeof what, "%d Hz: the grid's next_slot locks under fixed only", hz);
            check(grid.next_slot(kGameNs, 0, now, in_between, false, slot, frame_period) &&
                      !grid.next_slot(kGameNs, 0, now, in_between, true, slot, frame_period),
                  what);
            galaxy::host::FramePhase fixed_phase;
            galaxy::host::FramePhase variable_phase;
            const std::int64_t last = 5'000'000'000;
            // A refresh 3 ms off the cadence: a locked step moves its start toward it.
            const galaxy::host::DisplayRefresh fixed{last + 3'000'000, refresh, false};
            const galaxy::host::DisplayRefresh variable{last + 3'000'000, refresh, true};
            std::snprintf(what, sizeof what, "%d Hz: the frame phase follows the host's clock under variable", hz);
            check(variable_phase.next_start(last, 2'000'000, kGameNs, variable, in_between) == last + kGameNs &&
                      fixed_phase.next_start(last, 2'000'000, kGameNs, fixed, in_between) != last + kGameNs,
                  what);
        }
    }
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
    test_lead_at_multiples();
    test_variable_clock();
    test_variable_refresh();
    test_display_model();
    test_display_model_asked();
    test_display_model_answers();
    test_arrival_clock();
    test_unlimited();
    test_stall();
    test_waiting();
    test_gpu_governor();
    test_cadence_governor();
    test_walk_pairing();
    test_configure();
    test_rate_steps();
    test_refresh_proof();
    test_below_range();
    test_refresh_cap();
    test_variable_step_unlocked();
    if (failures != 0) {
        std::printf("%d failed\n", failures);
        return 1;
    }
    std::printf("frame schedule: all passed\n");
    return 0;
}
