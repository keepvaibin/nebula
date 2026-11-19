// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/frame_schedule.h"

#include "galaxy/game_step.h"
#include "galaxy/render/render_config.h"

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

void FrameSchedule::start(std::int64_t now, std::int64_t asked, std::int64_t step, std::uint64_t waited_total,
                          bool stopping) {
    const std::int64_t frequency = frequency_;
    const ScheduleSettings settings = display_.settings();
    double rate = std::max(kGameStepRate + 1.0, settings.present_rate);
    // Pictures shown whole from a refresh on (VSync, or a swap chain that cannot tear) with the display's vblanks timed:
    // every slot is a vblank (every n-th for a rate below the refresh, kept above 60), so each picture's moment is the
    // one its vblank shows. A slot clock of its own would beat against the refreshes there: two pictures in one refresh
    // (one never seen) and a refresh shown twice, over and over. The game's 60 Hz is not locked to the display, so
    // content time follows the slots (no lead). An unlimited rate with VSync off keeps no slots.
    std::int64_t vblank = 0;
    std::int64_t vblank_period = 0;
    const bool on_vblanks = (settings.vsync || std::isfinite(rate)) && display_.vblank_grid(vblank, vblank_period);
    std::int64_t slot_vblanks = 1;
    const auto fit_rate_to_vblanks = [&] {
        const double refresh = static_cast<double>(frequency) / static_cast<double>(vblank_period);
        slot_vblanks = vblanks_per_slot(refresh, rate);
        rate = refresh / static_cast<double>(slot_vblanks);
    };
    if (on_vblanks) {
        fit_rate_to_vblanks();
    }
    ++serial_;
    ++counts_.started;
    // The frame's arrival: when the game asked for its present (the arrival clock below says why).
    const std::int64_t seen = asked != 0 && asked <= now ? asked : now;
    note_cadence(seen, waited_total);
    sequence_.active = true;
    sequence_.generated = gpu_governor(rate, stopping, now);
    if (on_vblanks) {
        fit_rate_to_vblanks();  // a rate the governors lowered
    }
    sequence_.next = 0u;
    // Present slots run on one clock at the present rate. A slot shows the game at t = (slot - arrival) * 60 + lead
    // between the previous frame (0) and this one (1); the first slot with t >= 1 presents this frame itself. Integer
    // multiples of 60 lock the clock to each arrival with lead 60 / rate: k generated frames at i / (k + 1), then the
    // frame, on time. Other rates keep a free-running clock with lead 0: content time is then linear in present time
    // (the next arrival ends the sequence at t -> 1), where a lead would show the frame up to one slot early and step
    // the content unevenly around it. A clock left behind by a pause restarts.
    // An unlimited rate (render_config.h kUnlimitedFrameRate) has no slot clock: each picture is drawn as soon as the
    // one before is and shows the moment it is drawn for (next).
    unbounded_ = !on_vblanks && !std::isfinite(rate);
    slot_period_ = on_vblanks ? slot_vblanks * vblank_period
        : unbounded_          ? 0
                              : std::max<std::int64_t>(1, static_cast<std::int64_t>(static_cast<double>(frequency) / rate));
    const bool locked = !on_vblanks && !unbounded_ &&
                        std::abs(rate / kGameStepRate - std::round(rate / kGameStepRate)) < 0.02;
    // The arrival clock: a frame arrives when the game asks for its present (stamped on the game's thread as it queues
    // it), never when the render thread takes it: a take waits for the picture in progress, and at Unlimited for the
    // sequence before to reach t = 1, so take times would only ever look late and the smoothed period would climb to
    // its clamp, the takes then holding the game back. Arrivals are the game's frame cadence, smoothed: the prediction
    // one or more frame periods on, pulled 1/8 toward the measured arrival, with the period tracking the game's rate. A
    // miss by a third of a period or more (pause, hitch) restarts it at the measured time.
    if (frame_period_ == 0) {
        frame_period_ = frequency / kGameStepsPerSecond;
    }
    std::int64_t arrival = seen;
    const std::int64_t moment = step;
    sequence_.step_qpc = moment > 0 && moment <= seen ? moment : 0;
    if (moment > 0 && moment <= seen && seen - moment < frame_period_) {
        // A step clock's moment: the frame's own time, exact. Its pictures are shown a delay after it, the same for
        // every frame, so the content's time runs on the wall clock's (t linear in the present's moment across frames,
        // as the steps are). The delay covers the frames' usual lateness to the render thread (the game's update and
        // the list's making after the moment): the lateness's 97th percentile, tracked by a streaming estimate (up 0.97
        // of 0.1 ms after a later frame, down 0.03 of it after an earlier one), with a 0.2 ms guard. It moves by at most
        // 2 % of a period a frame up and 1 % down, so the content's speed changes by that and no more. A rarer late
        // frame holds its sequence at t = 1 until it comes and then shows its own moment: the content keeps to the wall
        // clock across it, as a display holds a late frame and goes on, and adds no lasting delay.
        const std::int64_t late = seen - moment;
        const std::int64_t period = frequency / kGameStepsPerSecond;
        const std::int64_t quantile_step = frequency / 10000;
        late_quantile_ = std::clamp<std::int64_t>(
            late_quantile_ + (late > late_quantile_ ? quantile_step * 97 / 100 : -(quantile_step * 3 / 100)), 0,
            period / 2);
        const std::int64_t wanted = std::min(late_quantile_ + frequency / 5000, period / 2);
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
    sequence_.frame_qpc = arrival;
    sequence_.lead = locked ? kGameStepRate / rate : 0.0;
    if (on_vblanks) {
        // The next slot moves to the vblank nearest where it was due, so the clock follows the display without adding
        // or dropping a slot; a clock not running starts at the vblank nearest now.
        const std::int64_t due = slot_due_ != 0 && std::llabs(slot_due_ - now) <= slot_period_ ? slot_due_ : now;
        slot_due_ = vblank + std::llround(static_cast<double>(due - vblank) / static_cast<double>(vblank_period)) *
                                 vblank_period;
    } else if (unbounded_) {
        slot_due_ = now;
    } else if (locked || slot_due_ == 0 || now - slot_due_ > slot_period_ || slot_due_ - now > slot_period_) {
        // Locked rates start the slots at the (smoothed) arrival.
        slot_due_ = locked && arrival <= now + slot_period_ ? arrival : now;
    }
    // With no generated pictures there is no interval to interpolate. A smoothed arrival slightly in the future must
    // not hold the only picture back while a busy queue replaces it with another sequence.
    if (sequence_.generated == 0u) {
        slot_due_ = std::min(slot_due_, now);
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
    shown_list_ = nullptr;
}

double FrameSchedule::slot_t() const noexcept {
    return static_cast<double>(slot_due_ - sequence_.frame_qpc) * kGameStepRate / static_cast<double>(frequency_) +
           sequence_.lead;
}

SchedulePicture FrameSchedule::next(std::int64_t now, bool flush) {
    SchedulePicture picture;
    // Slots run on the clock with VSync too: a present that waits for the display would hold back the frame chunks
    // behind it.
    if (!sequence_.active || (!flush && now < slot_due_)) {
        return picture;
    }
    // With no slot clock (an unlimited rate) the picture shows the moment it is drawn for.
    const std::int64_t slot = unbounded_ ? now : slot_due_;
    // A free-running slot due just before the arrival shows t = 0.
    const double t = std::max(0.0, static_cast<double>(slot - sequence_.frame_qpc) * kGameStepRate /
                                           static_cast<double>(frequency_) + sequence_.lead);
    if (!flush && unbounded_) {
        slot_due_ = now;
    } else if (!flush) {
        slot_due_ = slot + slot_period_;
        // A stall skips the slots it missed instead of bunching them.
        if (now >= slot_due_) {
            const std::int64_t missed = (now - slot_due_) / slot_period_ + 1;
            counts_.late_slots += static_cast<std::uint64_t>(missed);
            slot_due_ += missed * slot_period_;
        }
    }
    picture.slot = slot;
    picture.t = t;
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

// Only within a sequence: the next sequence starts its slots at the vblank nearest its frame (start).
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
