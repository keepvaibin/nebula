// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// When each picture between two game frames is shown and which moment of the game it shows (I-016): the present slot
// clock, the game frames' arrival clock, the step clock's delay, the two governors that lower the generated pictures
// when they would cost the game its frames, a sequence replaced before its frame, and the walk's history pairing.
// RenderCore draws what it answers. It has no renderer and no clock of its own: every call is given the time, and the
// display and the GPU are asked through ScheduleDisplay, so frame_schedule_tests.cpp drives it with simulated displays.
//
// A game frame's sequence: its generated pictures at t in (0, 1) on the present slots, then the frame itself (t = 1).
// The next game frame's sequence can replace it before its frame (at rates that are not multiples of 60 that is the
// normal end): its frame is then walked unseen, for what later frames read of it.

#include <array>
#include <cstdint>

namespace galaxy::render {

// The vblanks per present slot for a rate on a display of `refresh` hertz: a whole number, the rate kept above 60.
[[nodiscard]] std::int64_t vblanks_per_slot(double refresh, double rate);

struct ScheduleSettings {
    double present_rate = 0.0;
    bool vsync = false;
};

// What the schedule asks of the settings, the display and the renderer.
class ScheduleDisplay {
public:
    virtual ~ScheduleDisplay() = default;
    // The present rate the settings ask for (render_config.h present_frame_rate; infinite for an unlimited rate) and
    // whether VSync is on.
    [[nodiscard]] virtual ScheduleSettings settings() const = 0;
    // The display's vblanks: true where pictures show whole from a refresh on and the vblanks are timed.
    [[nodiscard]] virtual bool vblank_grid(std::int64_t& vblank, std::int64_t& period) const = 0;
    // A generated picture's GPU time in milliseconds as the renderer estimates it (a scene's walk); 0 when unknown.
    [[nodiscard]] virtual double generated_gpu_ms() const = 0;
    // The temporal upscaler runs on generated pictures.
    [[nodiscard]] virtual bool temporal_upscaling() const = 0;
    virtual void suspend_temporal_upscaling(double generated_ms) = 0;
    // The generated pictures' measured GPU time starts over.
    virtual void reset_generated_gpu_ms() = 0;
    // A benchmark runs: as many generated pictures as the GPU makes, no budget.
    [[nodiscard]] virtual bool benchmark() const = 0;
    // The governors' decisions are written to the error stream.
    [[nodiscard]] virtual bool tracing() const { return false; }
};

// The playing game frame's sequence.
struct ScheduleSequence {
    bool active = false;
    unsigned generated = 0;      // generated pictures it may show
    unsigned next = 0;           // generated pictures shown
    std::int64_t frame_qpc = 0;  // the frame's arrival
    double lead = 0.0;           // t of a slot at the arrival (FrameSchedule::start)
    std::int64_t step_qpc = 0;   // the moment its frame's step began (the step clock), 0 unknown
};

// The next picture of the playing sequence.
struct SchedulePicture {
    enum class Kind : std::uint8_t {
        None,       // nothing due yet, or no sequence
        Generated,  // a generated picture at t
        Frame,      // the frame itself; the sequence has ended
    };
    Kind kind = Kind::None;
    std::int64_t slot = 0;  // the moment it shows
    double t = 0.0;
    bool flush = false;  // the frame shown at once, not on its slot
};

// The walk's times for a picture: t, the t of the picture walked before it on the same pair of frames (motion), and
// whether the temporal history starts again.
struct ScheduleWalk {
    float t = 0.0f;
    float t_prev = 0.0f;
    bool reset = false;
};

// What the schedule counts, for the present statistics.
struct ScheduleCounts {
    std::uint64_t started = 0;
    std::uint64_t superseded = 0;
    std::uint64_t late_slots = 0;  // slots a stall skipped
    std::uint64_t gpu_capped = 0;  // sequences with fewer generated pictures
    std::uint64_t cadence_drops = 0;
    std::uint64_t history_resets = 0;
    std::uint64_t history_carried = 0;
    double delay_ms_max = 0.0;  // the step clock's largest delay
};

class FrameSchedule {
public:
    FrameSchedule(ScheduleDisplay& display, std::int64_t frequency) noexcept;

    // The generated pictures per game frame the present rate asks for (interpolated_frames_per_game_frame). A change
    // starts both governors over; none leaves no slot clock.
    void configure(unsigned generated, std::int64_t now);
    [[nodiscard]] unsigned configured() const noexcept { return frames_; }

    // A game frame's sequence starts. `asked`: when the game asked for its present, 0 unknown; `step`: its step's
    // moment, 0 unknown; `waited_total`: the game thread's time blocked on the render thread so far; `stopping`: the
    // render thread drains for a stop (no generated pictures).
    void start(std::int64_t now, std::int64_t asked, std::int64_t step, std::uint64_t waited_total, bool stopping);
    // A game frame presented while interpolating, with no sequence of its own.
    void frame_alone(std::int64_t now, std::uint64_t waited_total) { note_cadence(now, waited_total); }
    // The playing sequence is replaced before its frame: true when one was playing.
    bool supersede() noexcept;
    // Every sequence and pairing forgotten (a lost device).
    void forget() noexcept;

    // The picture due at `now` (all of what is left at once with `flush`); the slot clock moves past it.
    [[nodiscard]] SchedulePicture next(std::int64_t now, bool flush);
    // A generated picture was drawn in `spent` ticks; `presented`: it reached the swap chain.
    void generated_drawn(std::uint64_t spent, bool presented) noexcept;
    // The presentation times lower their depth by one picture: the next slot is skipped (PresentSlots::take_skip).
    void skip_slot() noexcept;

    // The walk's times for a picture at t of `list`, whose pair begins at `before`; `cut`: the camera cut. `count`
    // counts it in the statistics. walked() records it as the picture walked last.
    [[nodiscard]] ScheduleWalk walk_time(const void* list, const void* before, bool cut, float t, bool count);
    void walked(const void* list, float t) noexcept;
    void forget_walked() noexcept { shown_list_ = nullptr; }

    [[nodiscard]] const ScheduleSequence& sequence() const noexcept { return sequence_; }
    // t of the next slot in the playing sequence.
    [[nodiscard]] double slot_t() const noexcept;
    [[nodiscard]] std::int64_t slot_due() const noexcept { return slot_due_; }
    // A slot's length; 0 with no slot clock.
    [[nodiscard]] std::int64_t slot_period() const noexcept { return slot_period_; }
    // An unlimited present rate: no slot clock, each picture drawn as soon as the one before is.
    [[nodiscard]] bool unbounded() const noexcept { return unbounded_; }
    [[nodiscard]] std::int64_t remaining_us(std::int64_t now) const noexcept {
        return (slot_due_ - now) * 1'000'000 / frequency_;
    }

    // The present rate with `generated` generated pictures per game frame.
    [[nodiscard]] double rate_with(unsigned generated) const;
    // The most generated pictures below `generated` that present at a lower rate: 0 when none.
    [[nodiscard]] unsigned step_down(unsigned generated) const;
    // The fewest generated pictures above `generated` that present at a higher rate: all of them when none.
    [[nodiscard]] unsigned step_up(unsigned generated) const;

    [[nodiscard]] unsigned gpu_cap() const noexcept { return gpu_cap_; }
    [[nodiscard]] unsigned cadence_cap() const noexcept { return cadence_cap_; }
    [[nodiscard]] double game_fps() const noexcept { return cadence_fps_; }
    [[nodiscard]] std::int64_t delay_qpc() const noexcept { return delay_qpc_; }
    [[nodiscard]] std::uint64_t serial() const noexcept { return serial_; }
    // The counts since the last take; the step delay's maximum and the counts start over.
    [[nodiscard]] ScheduleCounts take_counts() noexcept;

private:
    unsigned gpu_governor(double& rate, bool stopping, std::int64_t now);
    void restart_governors(std::int64_t now);
    void note_cadence(std::int64_t now, std::uint64_t waited_total);

    ScheduleDisplay& display_;
    std::int64_t frequency_;
    unsigned frames_ = 0;
    ScheduleSequence sequence_{};
    std::uint64_t serial_ = 0;

    // The present slot clock: the next slot and a slot's length (on the display's vblanks when they are timed).
    std::int64_t slot_due_ = 0;
    std::int64_t slot_period_ = 0;
    bool unbounded_ = false;

    // The arrival clock and the step clock's delay (start).
    std::int64_t arrival_ = 0;
    std::int64_t frame_period_ = 0;
    std::int64_t delay_qpc_ = 0;
    std::int64_t late_quantile_ = 0;

    // The GPU governor.
    unsigned gpu_cap_;
    std::uint64_t gpu_cap_serial_ = 0;
    std::uint64_t gpu_checked_ = 0;
    std::int64_t gpu_warm_until_ = 0;

    // The cadence governor (note_cadence).
    unsigned cadence_cap_;
    unsigned cadence_interval_s_ = 2u;
    std::int64_t cadence_window_ = 0;
    std::int64_t cadence_last_ = 0;
    std::uint32_t cadence_misses_ = 0;  // in the window: the generated pictures'
    std::uint32_t cadence_own_ = 0;     // in the window: the game's own
    std::int64_t cadence_change_ = 0;
    std::int64_t cadence_try_ = 0;
    std::uint32_t cadence_frames_ = 0;
    std::int64_t cadence_grace_until_ = 0;  // misses after a change are not counted
    double cadence_fps_ = 0.0;
    double cadence_own_misses_ = 0.0;  // the game's own misses per second: their lowest recent rate
    std::uint64_t cadence_wait_seen_ = 0;
    std::uint64_t cadence_wait_ = 0;  // in the window
    std::uint64_t generated_spent_ = 0;
    std::uint64_t cadence_generated_seen_ = 0;
    std::uint64_t presented_total_ = 0;
    std::uint64_t cadence_presented_seen_ = 0;
    unsigned cadence_own_windows_ = 0;
    bool cadence_probe_ = false;
    unsigned cadence_probe_restore_ = 0;
    double cadence_probe_misses_ = 0.0;
    std::int64_t cadence_probe_after_ = 0;

    // The picture walked last.
    const void* shown_list_ = nullptr;
    std::uint64_t shown_serial_ = 0;
    float shown_t_ = 0.0f;

    ScheduleCounts counts_{};
};

}  // namespace galaxy::render
