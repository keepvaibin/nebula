// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/frame_schedule.h"

#include "galaxy/game_step.h"
#include "galaxy/render/render_config.h"
#include "galaxy/step_lock.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace galaxy::render {

std::int64_t vblanks_per_slot(double refresh, double rate) {
    std::int64_t slot = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(refresh / rate - 0.02)));
    if (refresh / static_cast<double>(slot) < kGameStepRate + 1.0) {
        slot = std::max<std::int64_t>(1, static_cast<std::int64_t>(refresh / (kGameStepRate + 1.0)));
    }
    return slot;
}

namespace {

// The first tick of a clock (`origin`, every `period`) at or after `moment`.
std::int64_t first_tick_from(std::int64_t origin, std::int64_t period, std::int64_t moment) {
    const std::int64_t from = moment - origin;
    const std::int64_t ticks = from >= 0 ? (from + period - 1) / period : -(-from / period);
    return origin + ticks * period;
}

}  // namespace

std::int64_t slots_per_step(double rate) {
    if (!std::isfinite(rate)) {
        return 0;
    }
    const std::int64_t slots = std::llround(rate / kGameStepRate);
    if (slots < 1) {
        return 0;
    }
    const double whole = static_cast<double>(slots) * kGameStepRate;
    return std::abs(rate - whole) * static_cast<double>(kStepLockNear) <= whole ? slots : 0;
}

std::int64_t DisplayModel::first_after(std::int64_t moment) const noexcept {
    const std::int64_t from = moment - refresh_;
    const std::int64_t whole = from >= 0 ? from / period_ : -((-from + period_ - 1) / period_);
    return refresh_ + (whole + 1) * period_;
}

void DisplayModel::presented(std::uint64_t id, std::int64_t slot, std::int64_t ready, std::int64_t call,
                             bool asked) noexcept {
    if (id <= last_id_) {
        reset();  // a new swap chain counts its presents from the start
    }
    last_id_ = id;
    presents_[next_] = {id, slot, ready, call, asked && slot != 0};
    next_ = (next_ + 1u) % presents_.size();
}

void DisplayModel::displayed(std::uint64_t id, std::int64_t shown, std::int64_t period) noexcept {
    if (period <= 0 || id <= last_displayed_) {
        return;  // no present shown since the last report
    }
    if (period_ != 0 && period != period_) {
        held_ = -1;  // another display mode: learnt again
        window_held_ = -1;
    }
    last_displayed_ = id;
    refresh_ = shown;
    period_ = period;
    const Present* present = nullptr;
    for (const Present& p : presents_) {
        if (p.id == id) {
            present = &p;
        }
    }
    if (present == nullptr) {
        return;
    }
    // The refreshes it was held past the first after its call: the queue's and the compositor's, and a GPU late for
    // that refresh. The least over a window is the display's own. A present that asked for a refresh later than the
    // model's for its call and was shown at it was held by its own time: it tells no more than that it was in time.
    const bool own = !present->asked || shown > present->slot + period / 2 ||
                     present->slot <= first_after(present->call) + std::max<std::int64_t>(held_, 0) * period;
    if (own) {
        const std::int64_t after = shown - present->call;
        const std::int64_t held = after <= 0 ? 0 : (after + period - 1) / period - 1;
        window_held_ = window_held_ < 0 ? held : std::min(window_held_, held);
        if (held_ < 0 || window_start_ == 0) {
            held_ = held;
            window_start_ = shown;
        }
    }
    if (held_ >= 0 && frequency_ > 0 && shown - window_start_ >= frequency_) {
        if (window_held_ >= 0) {
            held_ = window_held_;
        }
        window_held_ = -1;
        window_start_ = shown;
    }
    if (present->slot == 0 || held_ < 0) {
        return;
    }
    shown_.slot = present->slot;
    shown_.earliest = first_after(present->ready) + held_ * period;
    shown_.shown = shown;
    // A present reaches the refresh `held` after the first after it: the last that reaches the slot is made before the
    // refresh `held` before it.
    shown_.margin = present->slot - held_ * period - present->ready;
    shown_.period = period;
    fresh_ = true;
}

bool DisplayModel::display_time(std::int64_t present, std::int64_t& shown) const noexcept {
    shown = 0;
    if (held_ < 0 || period_ <= 0 || (frequency_ > 0 && present - refresh_ > frequency_)) {
        return false;
    }
    shown = first_after(present) + held_ * period_;
    return true;
}

std::int64_t DisplayModel::present_from(std::int64_t slot) const noexcept {
    if (held_ < 0 || period_ <= 0 || slot == 0) {
        return 0;
    }
    const std::int64_t guard = std::min(period_ / 4, frequency_ > 0 ? frequency_ / 2000 : period_ / 4);
    return slot - (held_ + 1) * period_ + guard;
}

bool DisplayModel::take_shown(ShownPicture& picture) noexcept {
    if (!fresh_) {
        return false;
    }
    picture = shown_;
    fresh_ = false;
    return true;
}

FrameSchedule::FrameSchedule(ScheduleDisplay& display, std::int64_t frequency) noexcept
    : display_(display), frequency_(frequency), gpu_cap_(kUnboundedGeneratedFrames), cadence_cap_(kUnboundedGeneratedFrames) {}

void FrameSchedule::configure(unsigned generated, std::int64_t now) {
    if (generated == frames_) {
        return;
    }
    frames_ = generated;
    restart_governors(now);
    if (generated == 0u) {
        // No slot clock left (RenderCore's overlay presents keep the game's own rate).
        slot_period_ = 0;
        unbounded_ = false;
    }
}

void FrameSchedule::start(std::int64_t now, std::int64_t arrived, std::int64_t step, bool stopping) {
    const std::int64_t frequency = frequency_;
    // The list's arrival: when the game handed it over (stamped on the game's thread as it queues it), never when the
    // render thread takes it: a take waits for the picture in progress, and at Unlimited for the sequence before to
    // reach t = 1, so take times would only ever look late.
    const std::int64_t seen = arrived != 0 && arrived <= now ? arrived : now;
    // The reference. With no step moment, the arrival clock: the game's frame cadence, smoothed: the prediction one or
    // more frame periods on, pulled 1/8 toward the measured arrival, with the period tracking the game's rate (the
    // smoothed period would otherwise climb to its clamp and the takes hold the game back). A miss by a third of a
    // period or more (pause, hitch) restarts it at the measured time.
    if (frame_period_ == 0) {
        frame_period_ = frequency / kGameStepsPerSecond;
    }
    std::int64_t arrival = seen;
    const std::int64_t moment = step;
    const std::int64_t step_qpc = moment > 0 && moment <= seen ? moment : 0;
    if (moment > 0 && moment <= seen && seen - moment < frame_period_) {
        // A step clock's moment: the frame's own time, exact. Its pictures are shown a delay after it, the same for
        // every frame, so the content's time runs on the wall clock's (t linear in the present's moment across frames,
        // as the steps are). The delay covers the lists' usual lateness to the render thread (the game's update and
        // the list's making after the moment): the lateness's 97th percentile, tracked by a streaming estimate (up 0.97
        // of 0.1 ms after a later list, down 0.03 of it after an earlier one), with a 0.2 ms guard, at most one step
        // (where the next step's present request started a sequence before I-004). It moves by at most 2 % of a period a
        // frame up and 1 % down, so the content's speed changes by that and no more. A rarer late list replaces the
        // sequence before at its first slot and shows its own moment: the content keeps to the wall clock across it,
        // as a display holds a late frame and goes on, and adds no lasting delay.
        const std::int64_t late = seen - moment;
        const std::int64_t period = frequency / kGameStepsPerSecond;
        const std::int64_t quantile_step = frequency / 10000;
        late_quantile_ = std::clamp<std::int64_t>(
            late_quantile_ + (late > late_quantile_ ? quantile_step * 97 / 100 : -(quantile_step * 3 / 100)), 0, period);
        const std::int64_t wanted = std::min(late_quantile_ + frequency / 5000, period);
        if (delay_qpc_ < wanted) {
            delay_qpc_ = std::min(wanted, delay_qpc_ + period / 50);
        } else if (delay_qpc_ > wanted) {
            delay_qpc_ = std::max(wanted, delay_qpc_ - period / 100);
        }
        counts_.delay_ms_max = std::max(counts_.delay_ms_max,
                                        static_cast<double>(delay_qpc_) * 1000.0 / static_cast<double>(frequency));
        arrival = moment + delay_qpc_;
    } else if (arrival_ != 0 && seen > arrival_) {
        const std::int64_t periods = std::max<std::int64_t>(1, (seen - arrival_ + frame_period_ / 2) / frame_period_);
        const std::int64_t predicted = arrival_ + periods * frame_period_;
        const std::int64_t error = seen - predicted;
        if (periods <= 4 && std::abs(error) < frame_period_ / 3) {
            arrival = predicted + error / 8;
            frame_period_ = std::clamp<std::int64_t>(frame_period_ + error / (64 * periods),
                                                     frequency / (kGameStepsPerSecond + 2),
                                                     frequency / (kGameStepsPerSecond - 2));
        }
    }
    arrival_ = arrival;
    // With a display model the slots are the refreshes the pictures show at, a display lead after they start: the
    // reference holds that lead, so a sequence's first picture starts after its list came. It moves toward the lead by
    // the delay's bounds (2 % of a step a frame up, 1 % down), at once where none was held.
    {
        const std::int64_t period = frequency / kGameStepsPerSecond;
        if (display_delay_qpc_ == 0 || display_lead_ == 0) {
            display_delay_qpc_ = display_lead_;
        } else if (display_delay_qpc_ < display_lead_) {
            display_delay_qpc_ = std::min(display_lead_, display_delay_qpc_ + period / 50);
        } else {
            display_delay_qpc_ = std::max(display_lead_, display_delay_qpc_ - period / 100);
        }
    }
    waiting_ = ScheduleSequence{};
    waiting_.active = true;
    waiting_.frame_qpc = arrival + display_delay_qpc_;
    waiting_.step_qpc = step_qpc;
    waiting_.stopping = stopping;
    // Nothing plays: the sequence begins at once (a slot at or before its reference waits, next). Else the playing one
    // shows the slots before the reference at their own t.
    if (!sequence_.active) {
        begin_waiting(now);
    }
}

void FrameSchedule::begin_waiting(std::int64_t now) {
    if (!waiting_.active) {
        return;
    }
    const std::int64_t frequency = frequency_;
    const ScheduleSettings settings = display_.settings();
    // A display whose refreshes follow the presents: the slot clock is the pacer's own, at the player's limit when that
    // is at or under the display's cap, else at the cap; its vblanks are never the clock.
    double cap = 0.0;
    const bool variable = display_.variable_refresh(cap);
    double rate = std::max(kGameStepRate + 1.0,
                           variable && cap > 1.0 ? std::min(settings.present_rate, cap) : settings.present_rate);
    // Pictures shown whole from a refresh on (VSync, or a swap chain that cannot tear) with the display's vblanks timed:
    // every slot is a vblank (every n-th for a rate below the refresh, kept above 60), so each picture's moment is the
    // one its vblank shows. A slot clock of its own would beat against the refreshes there: two pictures in one refresh
    // (one never seen) and a refresh shown twice, over and over. An unlimited rate with VSync off keeps no slots.
    std::int64_t vblank = 0;
    std::int64_t vblank_period = 0;
    const bool on_vblanks =
        !variable && (settings.vsync || std::isfinite(rate)) && display_.vblank_grid(vblank, vblank_period);
    std::int64_t slot_vblanks = 1;
    const auto fit_rate_to_vblanks = [&] {
        const double refresh = static_cast<double>(frequency) / static_cast<double>(vblank_period);
        slot_vblanks = vblanks_per_slot(refresh, rate);
        rate = refresh / static_cast<double>(slot_vblanks);
    };
    if (on_vblanks) {
        fit_rate_to_vblanks();
    }
    // The display model's lead, on the vblanks only (a clock of the pacer's own has no refresh to ask for).
    observe_shown();
    const bool lead_was = display_lead_ != 0;
    display_lead_ = on_vblanks ? display_lead_for(now, vblank_period) : 0;
    ++serial_;
    ++counts_.started;
    sequence_ = std::exchange(waiting_, ScheduleSequence{});
    const std::int64_t arrival = sequence_.frame_qpc;
    sequence_.generated = gpu_governor(rate, sequence_.stopping, now);
    if (on_vblanks) {
        fit_rate_to_vblanks();  // a rate the governors lowered
    }
    sequence_.next = 0u;
    // Present slots run on one clock at the present rate. A slot shows the game at t = (slot - reference) * 60 + lead
    // between the previous frame (0) and this one (1); the first slot with t >= 1 presents this frame itself. A slot
    // clock of k times 60 whose ticks the step's start keeps to locks to each reference with lead 1/k: k - 1 generated
    // pictures at i / k, then the frame itself on a slot, so no walk of it is thrown away (I-002). That clock is the
    // display's vblanks when every vblank is a slot and the step is locked to them (galaxy/step_lock.h, exact with
    // in-between pictures on: the host's frame phase and display clocks start each step on the display's cadence), the
    // reference then moved to the first vblank at or after it (the nearest one can lie before the list came, where the
    // delay is over half a vblank: at 480 Hz); or the pacer's own clock (a swap chain that tears, a variable
    // refresh), the step on the host's clock at exactly 60 and the slots spaced from the reference itself. Other rates
    // keep lead 0 and a free-running clock (on vblanks, the vblanks): content time is then linear in present time (the
    // next reference ends the sequence at t -> 1), where a lead would show the frame up to one slot early and step the
    // content unevenly around it. A clock left behind by a pause restarts.
    // An unlimited rate (render_config.h kUnlimitedFrameRate) has no slot clock: each picture is drawn as soon as the
    // one before is and shows the moment it is drawn for (next).
    unbounded_ = !on_vblanks && !std::isfinite(rate);
    slot_period_ = on_vblanks ? slot_vblanks * vblank_period
        : unbounded_          ? 0
                              : std::max<std::int64_t>(1, static_cast<std::int64_t>(static_cast<double>(frequency) / rate));
    // A lead needs pictures between the frames: none at 60 (every picture a real one) or with none generated.
    const std::int64_t slots = unbounded_ || sequence_.generated == 0u ? 0 : slots_per_step(rate);
    const bool locked_to_vblanks =
        on_vblanks && slots != 0 && slot_vblanks == 1 &&
        step_lock_refreshes(kGameStepNs,
                            std::llround(static_cast<double>(vblank_period) * 1e9 / static_cast<double>(frequency)),
                            true, false) == slots;  // on vblanks: the display is not taken as variable
    const bool locked = locked_to_vblanks || (!on_vblanks && slots != 0);
    sequence_.lead = locked ? 1.0 / static_cast<double>(slots) : 0.0;
    if (locked_to_vblanks) {
        sequence_.frame_qpc = first_tick_from(vblank, vblank_period, arrival);
    }
    if (on_vblanks) {
        // The next slot moves to the vblank nearest where it was due, so the clock follows the display without adding
        // or dropping a slot; a clock not running starts at the vblank nearest a display lead from now.
        const std::int64_t due =
            slot_due_ != 0 && std::llabs(slot_due_ - display_lead_ - now) <= slot_period_ ? slot_due_ : now + display_lead_;
        slot_due_ = vblank + std::llround(static_cast<double>(due - vblank) / static_cast<double>(vblank_period)) *
                                 vblank_period;
    } else if (unbounded_) {
        slot_due_ = now;
    } else if (!locked) {
        if (slot_due_ == 0 || now - slot_due_ > slot_period_ || slot_due_ - now > slot_period_) {
            slot_due_ = now;
        }
    } else {
        // Locked rates put the slots on the reference's clock: from the reference, or the first of its slots from now
        // when it is further ahead, never at or before the slot last shown while the clock runs.
        std::int64_t due =
            arrival > now + slot_period_ ? arrival - (arrival - now) / slot_period_ * slot_period_ : arrival;
        if (slot_due_ != 0 && std::llabs(slot_due_ - now) <= slot_period_ && due <= slot_due_ - slot_period_) {
            due += ((slot_due_ - slot_period_ - due) / slot_period_ + 1) * slot_period_;
        }
        slot_due_ = due;
    }
    // With no generated pictures there is no interval to interpolate. A reference slightly in the future must not hold
    // the only picture back while a busy queue replaces it with another sequence.
    if (sequence_.generated == 0u) {
        slot_due_ = std::min(slot_due_, now + display_lead_);
    }
    // The pictures drawn before the lead came in were not drawn for their refreshes: the lead's rule leaves them out.
    if (display_lead_ != 0 && !lead_was) {
        display_from_ = slot_due_;
    }
}

std::int64_t FrameSchedule::to_ns(std::int64_t ticks) const noexcept {
    return ticks / frequency_ * 1'000'000'000 + ticks % frequency_ * 1'000'000'000 / frequency_;
}

std::int64_t FrameSchedule::display_lead_for(std::int64_t now, std::int64_t vblank_period) {
    std::int64_t shown = 0;
    if (vblank_period <= 0 || !display_.display_time(now, shown) || shown <= now) {
        return 0;
    }
    // At the least the time from a present made now to the refresh that shows it, in whole refreshes; a refresh more
    // at first (the picture's own work); four game steps at most (present_slots.h).
    const std::int64_t unit = to_ns(vblank_period);
    const std::int64_t least = to_ns(shown - now);
    const std::int64_t depth = display_depth_.depth(least + unit, least, 4 * kGameStepNs, unit);
    return (depth + unit / 2) / unit * vblank_period;
}

void FrameSchedule::observe_shown() {
    ShownPicture picture;
    if (display_.last_shown(picture) && display_lead_ != 0 && picture.period > 0 && picture.slot >= display_from_) {
        const std::int64_t period = to_ns(picture.period);
        display_depth_.observe(to_ns(picture.slot), to_ns(picture.earliest), to_ns(picture.shown),
                               to_ns(picture.margin), period, period / 2);
    }
}

// As many generated frames per game frame as the GPU fits in nine tenths of a game step. A generated frame walks the
// whole frame list, so a game frame costs about (k + 1) generated frame times. k drops at once when over budget and
// rises after 2 s with current native scene timings (10 s for temporal reconstruction). Only temporal reconstruction
// needs a generated-frame retry; native scene frames keep its cost estimate live. A capped sequence runs the slot clock
// at 60 * (k + 1).
unsigned FrameSchedule::gpu_governor(double& rate, bool stopping, std::int64_t now) {
    constexpr double budget_ms = 0.9 * 1000.0 / kGameStepRate;
    const unsigned configured = stopping ? 0u : frames_;
    if (configured == 0u) {
        return configured;
    }
    const unsigned allowed = std::min(configured, cadence_cap_);
    // A benchmark shows as many generated frames as the GPU makes (render_config.h apply_benchmark_presenting): no
    // budget.
    if (display_.benchmark()) {
        if (allowed >= configured) {
            return configured;
        }
        ++counts_.gpu_capped;
        rate = kGameStepRate * static_cast<double>(allowed + 1u);
        return allowed;
    }
    if (serial_ - gpu_checked_ >= 30u && now >= gpu_warm_until_) {
        gpu_checked_ = serial_;
        const double generated_ms = display_.generated_gpu_ms();
        if (generated_ms > 0.0) {
            // With the temporal upscaler on generated frames the frame at t = 1 is one too; when no generated frame fits
            // beside it, the game frames take the upscaler instead (the same frames for less GPU time).
            const bool upscaled = display_.temporal_upscaling();
            const double fit = std::floor(budget_ms / generated_ms) - (upscaled ? 2.0 : 1.0);
            if (upscaled && fit < 1.0) {
                display_.suspend_temporal_upscaling(generated_ms);
            }
            const unsigned fits = fit < 1.0 ? 0u : static_cast<unsigned>(std::min(fit, static_cast<double>(configured)));
            if (fits < gpu_cap_) {
                gpu_cap_ = fits;
                gpu_cap_serial_ = serial_;
            } else if (fits > gpu_cap_ && serial_ - gpu_cap_serial_ >= (display_.temporal_upscaling() ? 600u : 120u)) {
                gpu_cap_ = std::min(fits, step_up(gpu_cap_));
                gpu_cap_serial_ = serial_;
            }
        }
        // Temporal reconstruction needs its own generated-frame measurement. Native replay has a fresh measurement
        // from every completed scene frame, so never inject a known over-budget probe into play.
        if (display_.temporal_upscaling() && gpu_cap_ == 0u && serial_ - gpu_cap_serial_ >= 1200u) {
            gpu_cap_ = 1u;  // measure one generated frame again
            gpu_cap_serial_ = serial_;
            display_.reset_generated_gpu_ms();
        }
    }
    const unsigned generated = std::min(allowed, gpu_cap_);
    if (generated >= configured) {
        return configured;
    }
    ++counts_.gpu_capped;
    rate = kGameStepRate * static_cast<double>(generated + 1u);
    return generated;
}

// Generated frames must never cost the game its own frames. Each game frame shown is timed against the frame period: an
// interval above 1.5 periods missed VIs. A miss is the generated frames' when the interval would not have missed without
// their render-thread time: the game waited on the render thread, and would have waited that much less (at most what
// it waited). The rest are the game's own, as a scene's first frames, whose decoding, uploads and pipelines keep the
// render thread far longer than generated frames do. Two or more of the generated frames' misses a second, and no
// fewer than the game's own, take generated frames away to the next lower present rate (to half of them at eight). The
// game's own misses three seconds in a row above their lowest rate are probed, for threads that share a CPU's cores:
// the next lower rate for a second, kept only if the misses halve. A try of the next higher rate is undone at its first
// miss of the generated frames'. Tries and probes follow a clean interval that doubles after each failed one (2 s to
// 64 s: a scene's first frames back the render thread up for a second or two) and halves after a try that held.
void FrameSchedule::restart_governors(std::int64_t now) {
    gpu_cap_ = kUnboundedGeneratedFrames;
    gpu_cap_serial_ = serial_;
    gpu_checked_ = serial_;
    gpu_warm_until_ = now + frequency_;  // one second of generated-frame times left out
    display_.reset_generated_gpu_ms();
    cadence_cap_ = kUnboundedGeneratedFrames;
    cadence_interval_s_ = 2u;
    cadence_try_ = 0;
    cadence_change_ = now;
    cadence_last_ = 0;
    cadence_grace_until_ = now + frequency_ * 3 / 2;
    cadence_own_windows_ = 0u;
    cadence_probe_ = false;
    cadence_probe_after_ = now;
}

void FrameSchedule::note_cadence(std::int64_t now, std::uint64_t waited_total) {
    const std::int64_t frequency = frequency_;
    const bool trace = display_.tracing();
    // The game frame period the arrivals follow (start's smoothed estimate), else the game step.
    const std::int64_t period = frame_period_ != 0 ? frame_period_ : frequency / kGameStepsPerSecond;
    // Since the frame before: the game's waits on the render thread, and the render thread's generated frame time.
    const auto waited = static_cast<std::int64_t>(waited_total - cadence_wait_seen_);
    const auto generated_time = static_cast<std::int64_t>(generated_spent_ - cadence_generated_seen_);
    cadence_wait_seen_ = waited_total;
    cadence_generated_seen_ = generated_spent_;
    const auto restart_window = [&] {
        cadence_window_ = now;
        cadence_frames_ = 0u;
        cadence_misses_ = 0u;
        cadence_own_ = 0u;
        cadence_wait_ = 0u;
        cadence_presented_seen_ = presented_total_;
    };
    if (cadence_last_ == 0 || now - cadence_last_ > 4 * frequency) {
        // First frame, or a pause (menu, load): a new window.
        cadence_last_ = now;
        restart_window();
        return;
    }
    const std::int64_t interval = now - cadence_last_;
    cadence_last_ = now;
    if (now < cadence_grace_until_) {
        // Just after a rate or upscaler change: its one-time work is no measure.
        restart_window();
        return;
    }
    ++cadence_frames_;
    cadence_wait_ += static_cast<std::uint64_t>(waited);
    const auto missed_in = [period](std::int64_t span) {
        return span > period * 3 / 2 ? static_cast<std::uint32_t>((span + period / 2) / period) - 1u : 0u;
    };
    const std::uint32_t missed = missed_in(interval);
    const std::uint32_t own = missed_in(interval - std::min(waited, generated_time));
    cadence_misses_ += missed - own;
    cadence_own_ += own;
    unsigned running = std::min({frames_, cadence_cap_, gpu_cap_});
    if (running == kUnboundedGeneratedFrames) {
        // No bound (an unlimited rate): the generated frames a game frame showed in this window are the count running.
        const std::uint64_t shown =
            (presented_total_ - cadence_presented_seen_) / std::max<std::uint64_t>(cadence_frames_, 1u);
        running = static_cast<unsigned>(std::min<std::uint64_t>(shown, kUnboundedGeneratedFrames - 1u));
    }
    const bool trying = cadence_try_ != 0 && now - cadence_try_ < 4 * frequency;
    const auto drop = [&](unsigned cap, const char* why) {
        if (trace) {
            std::cerr << "[interp-cadence] " << why << ' ' << running << "->" << cap << '\n';
        }
        if (trying) {
            cadence_interval_s_ = std::min(cadence_interval_s_ * 2u, 64u);
        }
        // The walk at t = 1 alone costs the game its frames when it takes a quarter of a frame or more.
        const double generated_ms = display_.generated_gpu_ms();
        if (cap == 0u && display_.temporal_upscaling() && generated_ms * 4.0 >= 1000.0 / kGameStepRate) {
            display_.suspend_temporal_upscaling(generated_ms);
        }
        cadence_cap_ = cap;
        cadence_change_ = now;
        cadence_try_ = 0;
        ++counts_.cadence_drops;
    };
    if (trying && missed != own && running > 0u) {
        drop(step_down(running), "try undone");
        return;
    }
    const std::int64_t elapsed = now - cadence_window_;
    if (elapsed < frequency) {
        return;
    }
    const double seconds = static_cast<double>(elapsed) / static_cast<double>(frequency);
    cadence_fps_ = static_cast<double>(cadence_frames_) / seconds;
    const double theirs = static_cast<double>(cadence_misses_) / seconds;
    const double own_rate = static_cast<double>(cadence_own_) / seconds;
    if (trace && cadence_misses_ + cadence_own_ != 0u) {
        std::cerr << "[interp-cadence] window generated-misses=" << cadence_misses_ << " own-misses=" << cadence_own_
                  << " running=" << running
                  << " game-wait-ms=" << static_cast<double>(cadence_wait_) * 1000.0 / static_cast<double>(frequency)
                  << " seconds=" << seconds << '\n';
    }
    restart_window();
    if (cadence_probe_) {
        // The probe's second: misses that halved with fewer generated frames were theirs, and the lower rate stays;
        // otherwise they are the game's own at that level, and the generated frames come back.
        cadence_probe_ = false;
        cadence_own_windows_ = 0u;
        if (own_rate <= cadence_probe_misses_ * 0.5) {
            cadence_change_ = now;
            ++counts_.cadence_drops;
        } else {
            if (trace) {
                std::cerr << "[interp-cadence] probe undone " << cadence_cap_ << "->" << cadence_probe_restore_ << '\n';
            }
            cadence_cap_ = cadence_probe_restore_;
            cadence_own_misses_ = cadence_probe_misses_;
            cadence_interval_s_ = std::min(cadence_interval_s_ * 2u, 64u);
        }
        cadence_probe_after_ = now + static_cast<std::int64_t>(cadence_interval_s_) * frequency;
        return;
    }
    const bool own_excess = running > 0u && own_rate - cadence_own_misses_ >= 2.0;
    cadence_own_misses_ = std::min(cadence_own_misses_, own_rate);
    cadence_own_windows_ = own_excess ? cadence_own_windows_ + 1u : 0u;
    if (running > 0u && theirs >= 2.0 && theirs >= own_rate) {
        drop(theirs >= 8.0 ? std::min(step_down(running), running / 2u) : step_down(running), "drop");
        return;
    }
    if (cadence_own_windows_ >= 3u && now >= cadence_probe_after_) {
        if (trace) {
            std::cerr << "[interp-cadence] probe " << running << "->" << step_down(running) << '\n';
        }
        cadence_probe_ = true;
        cadence_probe_restore_ = cadence_cap_;
        cadence_probe_misses_ = own_rate;
        cadence_cap_ = step_down(running);
        cadence_try_ = 0;
        return;
    }
    if (cadence_try_ != 0 && now - cadence_try_ >= 4 * frequency) {
        // The try held.
        cadence_interval_s_ = std::max(cadence_interval_s_ / 2u, 2u);
        cadence_try_ = 0;
    }
    if (cadence_cap_ >= frames_) {
        return;
    }
    if (now - cadence_change_ >= static_cast<std::int64_t>(cadence_interval_s_) * frequency) {
        const unsigned up = step_up(cadence_cap_);
        if (trace) {
            std::cerr << "[interp-cadence] try " << cadence_cap_ << "->" << up << '\n';
        }
        cadence_cap_ = up;
        cadence_change_ = now;
        cadence_try_ = now;
    }
}

// The present rate with `generated` generated frames per game frame (start): with all of them the target, else 60 a
// second for the frame and each generated frame; on timed vblanks (start's on_vblanks), a whole number of vblanks a
// slot.
double FrameSchedule::rate_with(unsigned generated) const {
    if (generated == 0u) {
        return kGameStepRate;
    }
    const ScheduleSettings settings = display_.settings();
    double rate = generated < frames_ ? kGameStepRate * static_cast<double>(generated + 1u)
                                      : std::max(kGameStepRate + 1.0, settings.present_rate);
    std::int64_t vblank = 0;
    std::int64_t vblank_period = 0;
    if ((settings.vsync || std::isfinite(rate)) && display_.vblank_grid(vblank, vblank_period) && vblank_period > 0) {
        const double refresh = static_cast<double>(frequency_) / static_cast<double>(vblank_period);
        rate = refresh / static_cast<double>(vblanks_per_slot(refresh, rate));
    }
    return rate;
}

// On a 240 Hz display two generated frames show 120 Hz, as one does: the step goes to the fewer.
unsigned FrameSchedule::step_down(unsigned generated) const {
    const unsigned from = std::min(generated, frames_);
    const double rate = rate_with(from);
    for (unsigned k = from; k > 0u; --k) {
        if (rate_with(k - 1u) < rate - 0.5) {
            return k - 1u;
        }
    }
    return 0u;
}

// With no bound (an unlimited rate) all of them: as many as the device makes (the GPU budget still bounds play,
// gpu_governor).
unsigned FrameSchedule::step_up(unsigned generated) const {
    if (frames_ == kUnboundedGeneratedFrames) {
        return frames_;
    }
    const double rate = rate_with(std::min(generated, frames_));
    for (unsigned k = generated + 1u; k < frames_; ++k) {
        if (rate_with(k) > rate + 0.5) {
            return k;
        }
    }
    return frames_;
}

bool FrameSchedule::supersede() noexcept {
    if (!sequence_.active) {
        return false;
    }
    sequence_.active = false;
    ++counts_.superseded;
    return true;
}

void FrameSchedule::forget() noexcept {
    sequence_ = {};
    waiting_ = {};
    shown_list_ = nullptr;
}

double FrameSchedule::t_at(std::int64_t slot, std::int64_t reference, double lead) const noexcept {
    return static_cast<double>(slot - reference) * kGameStepRate / static_cast<double>(frequency_) + lead;
}

double FrameSchedule::slot_t() const noexcept {
    return t_at(slot_due_, sequence_.frame_qpc, sequence_.lead);
}

SchedulePicture FrameSchedule::next(std::int64_t now, bool flush) {
    SchedulePicture picture;
    observe_shown();
    // Slots run on the clock with VSync too: a present that waits for the display would hold back the frame chunks
    // behind it. A slot's picture starts the display lead before it.
    if (!busy() || (!flush && now < slot_due())) {
        return picture;
    }
    // With no slot clock (an unlimited rate) the picture shows the moment it is drawn for.
    const std::int64_t slot = unbounded_ ? now : slot_due_;
    if (waiting_.active) {
        // The waiting sequence begins at the first slot past its reference, or at once when nothing plays; until then
        // the playing sequence shows the slots at its own t (N-1084). Its t is read with the playing one's lead, which
        // it takes as it begins unless the rate changed. A slot where it is at 0 shows the playing one's frame. On a
        // locked clock (a lead) its first slot is the first at or after its reference (begin_waiting), so the slot
        // before, where the playing one shows its own frame, never begins it.
        std::int64_t reference = waiting_.frame_qpc;
        if (sequence_.lead > 0.0 && slot_period_ > 0) {
            reference = first_tick_from(slot, slot_period_, reference);
        }
        if (flush || !sequence_.active || t_at(slot, reference, sequence_.lead) > 1e-3) {
            picture.kind = SchedulePicture::Kind::Begin;
            picture.slot = slot;
            picture.flush = flush;
            return picture;
        }
    }
    const double t = t_at(slot, sequence_.frame_qpc, sequence_.lead);
    if (!flush && t <= 0.0 && sequence_.generated != 0u) {
        // A slot at or before the reference with no sequence before it to show: it waits, the display showing the
        // picture before (t = 0 is that picture again). The slot clock moves to the first slot past the reference.
        const std::int64_t zero =
            sequence_.frame_qpc -
            static_cast<std::int64_t>(std::llround(sequence_.lead * static_cast<double>(frequency_) / kGameStepRate));
        if (unbounded_) {
            slot_due_ = zero + 1;
            ++counts_.waited_slots;
        } else {
            const std::int64_t waited = std::max<std::int64_t>(1, (zero - slot) / slot_period_ + 1);
            slot_due_ = slot + waited * slot_period_;
            counts_.waited_slots += static_cast<std::uint64_t>(waited);
        }
        return picture;
    }
    if (!flush && unbounded_) {
        slot_due_ = now;
    } else if (!flush) {
        slot_due_ = slot + slot_period_;
        // A stall skips the slots it missed instead of bunching them (with a display model: whose start it missed).
        if (now >= slot_due()) {
            const std::int64_t missed = (now - slot_due()) / slot_period_ + 1;
            counts_.late_slots += static_cast<std::uint64_t>(missed);
            slot_due_ += missed * slot_period_;
        }
    }
    picture.slot = slot;
    picture.t = std::max(0.0, t);  // 0 or below only for a frame shown at once (flush, no generated pictures)
    picture.flush = flush;
    // Generated pictures until t reaches 1 (the frame itself) or the next game frame replaces the sequence: no count
    // bounds them.
    if (!flush && t < 1.0 - 1e-3 && sequence_.generated != 0u) {
        picture.kind = SchedulePicture::Kind::Generated;
        return picture;
    }
    sequence_.active = false;
    picture.kind = SchedulePicture::Kind::Frame;
    return picture;
}

void FrameSchedule::generated_drawn(std::uint64_t spent, bool presented) noexcept {
    generated_spent_ += spent;
    if (presented) {
        ++presented_total_;
    }
    ++sequence_.next;
}

// Only within a sequence: the next sequence starts its slots at the vblank nearest its frame (begin_waiting).
void FrameSchedule::skip_slot() noexcept {
    if (slot_period_ > 0) {
        slot_due_ += slot_period_;
    }
}

// The picture walked before, on this pair's clock: motion to it is what the upscaler and the motion targets see.
// Anything else (a frame presented alone, a cut, skipped lists) starts the upscaler's history again.
ScheduleWalk FrameSchedule::walk_time(const void* list, const void* before, bool cut, float t, bool count) {
    ScheduleWalk walk;
    walk.t = t;
    walk.t_prev = t;
    const bool walked_before = shown_list_ != nullptr && shown_list_ == before && !cut;
    if (shown_list_ == list && shown_serial_ == serial_ && shown_t_ < t) {
        walk.t_prev = shown_t_;
    } else if (shown_t_ >= 1.0f && walked_before) {
        walk.t_prev = 0.0f;
    } else if (walked_before && shown_serial_ + 1u == serial_) {
        walk.t_prev = shown_t_ - 1.0f;
        counts_.history_carried += count ? 1u : 0u;
    } else {
        walk.reset = true;
        counts_.history_resets += count ? 1u : 0u;
    }
    return walk;
}

void FrameSchedule::walked(const void* list, float t) noexcept {
    shown_serial_ = serial_;
    shown_t_ = t;
    shown_list_ = list;
}

ScheduleCounts FrameSchedule::take_counts() noexcept {
    return std::exchange(counts_, ScheduleCounts{});
}

}  // namespace galaxy::render
