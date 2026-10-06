#pragma once

#include "galaxy/gx/frame_completion.h"
#include "galaxy/runtime_timeline.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace galaxy::vi {

enum class BoundaryPhase : std::uint8_t {
    Empty,
    Captured,
    SubmitPending,
    TokenPending,
    TokenConsumed,
    PeDrain,
    ViLevelLatched,
    AwaitViRfi,
    Finalize,
};

// This is checkpoint transaction order, not interrupt-source priority.  Wii
// devices latch their own levels and Broadway enters one external exception;
// RMGE01's translated dispatcher then reads the combined PI/device state and
// selects the registered OS interrupt.  A consumed VI edge is different from
// an ordinary device probe: its PendingBoundary is durable state that must be
// resumed before any later checkpoint phase can transfer to guest code again.
enum class CheckpointLeadService : std::uint8_t {
    None,
    Audio,
    Video,
};

[[nodiscard]] constexpr bool checkpoint_video_service_due(
    bool pending_boundary_active,
    bool cadence_unarmed,
    bool deadline_published) noexcept {
    return pending_boundary_active || cadence_unarmed || deadline_published;
}

[[nodiscard]] constexpr CheckpointLeadService select_checkpoint_lead_service(
    bool video_service_due,
    bool audio_service_due) noexcept {
    if (video_service_due) {
        return CheckpointLeadService::Video;
    }
    return audio_service_due ? CheckpointLeadService::Audio
                             : CheckpointLeadService::None;
}

struct BoundaryCapture {
    std::uint64_t serial{};
    timing::DeadlineEvent edge{};
    std::uint32_t interrupted_resume_pc{};
    std::uint32_t interrupted_context{};
    std::vector<std::byte> fifo{};
    bool use_realtime_vi{};
    bool present_this_vi{};
    bool present_in_render_frame{};
    bool render_completion_independent{};
    std::uint64_t xfb_copies_before_render{};
    std::chrono::steady_clock::time_point started{};
};

struct BoundarySnapshot {
    std::uint64_t serial{};
    BoundaryPhase phase{BoundaryPhase::Empty};
    timing::DeadlineEvent edge{};
    std::uint32_t interrupted_resume_pc{};
    std::uint32_t interrupted_context{};
    std::size_t rendered_fifo_size{};
    gx::FramePeCompletionToken token{};
    bool token_consumed{};
    bool render_completion_independent{};
    std::uint64_t detached_receipt_serial{};
    bool use_realtime_vi{};
    bool present_this_vi{};
    bool present_in_render_frame{};
    bool rendered_retrace_frame{};
    std::uint64_t xfb_copies_before_render{};
    std::uint64_t xfb_copies_after_present{};
    bool vi_rfi_seen{};
    std::chrono::steady_clock::time_point started{};
};

// TokenPending precedes VI level assertion and therefore has no translated VI
// dispatcher owner. Recovery may use the token as the durable owner only when
// no other external dispatcher is active and no stale VI-owner serial remains.
[[nodiscard]] constexpr bool
active_boundary_has_exclusive_pre_delivery_token(
    const BoundarySnapshot& boundary,
    bool external_dispatch_active,
    std::uint64_t external_dispatch_vi_owner_serial) noexcept {
    return boundary.phase == BoundaryPhase::TokenPending &&
        boundary.serial != 0u && static_cast<bool>(boundary.token) &&
        !boundary.token_consumed && !boundary.vi_rfi_seen &&
        !external_dispatch_active && external_dispatch_vi_owner_serial == 0u;
}

// A host suspension can expire one new VI deadline while the immediately
// preceding edge is still durably owned either by its renderer token or by its
// translated IRQ24 transaction. In that case the consumer correctly reports
// Ready rather than Backlog. TokenPending is safe only with one live,
// unconsumed token; the caller additionally proves that no external dispatcher
// owns the pre-delivery phase. This predicate proves that the active
// transaction owns exactly the preceding VI edge and that the last
// guest-visible time sample lies strictly before the ready successor. It does
// not prove a host suspension by itself; callers must separately require the
// wall/CPU interval, freshness, and device-deadline evidence before rebasing
// the shared timeline.
[[nodiscard]] constexpr bool active_boundary_owns_ready_vi_successor(
    const BoundarySnapshot& boundary,
    const timing::PeriodicDeadlineObservation& observation,
    std::uint64_t last_guest_ticks) noexcept {
    const bool token_pending =
        boundary.phase == BoundaryPhase::TokenPending &&
        static_cast<bool>(boundary.token) && !boundary.token_consumed;
    const bool awaiting_vi_rfi = boundary.phase == BoundaryPhase::AwaitViRfi;
    if ((!token_pending && !awaiting_vi_rfi) ||
        boundary.serial == 0u || boundary.vi_rfi_seen ||
        boundary.edge.kind != timing::EventKind::VideoInterface ||
        boundary.edge.replaceable || boundary.edge.sequence == 0u ||
        boundary.edge.deadline_ticks == 0u ||
        boundary.interrupted_resume_pc == 0u ||
        boundary.interrupted_context == 0u ||
        observation.status != timing::PeriodicConsumeStatus::Ready ||
        observation.expected_sequence == 0u ||
        observation.expected_deadline_ticks == 0u ||
        observation.pending_edges != 1u ||
        observation.scheduled_due_edges != 1u ||
        observation.missed_or_coalesced_edges != 0u ||
        observation.lateness_ticks >= timing::kViPeriodTicks ||
        last_guest_ticks < boundary.edge.deadline_ticks ||
        last_guest_ticks >= observation.expected_deadline_ticks ||
        boundary.edge.sequence ==
            std::numeric_limits<std::uint64_t>::max() ||
        boundary.edge.deadline_ticks >
            std::numeric_limits<std::uint64_t>::max() -
                timing::kViPeriodTicks) {
        return false;
    }
    return boundary.edge.sequence + 1u == observation.expected_sequence &&
        boundary.edge.deadline_ticks + timing::kViPeriodTicks ==
            observation.expected_deadline_ticks;
}

// Used by the default-on active-boundary host-suspension recovery. The call
// site can disable that recovery with GALAXY_ACTIVE_BOUNDARY_RECOVERY=0 for a
// controlled A/B diagnostic.
//
// active_boundary_owns_ready_vi_successor above proves recovery is safe only
// when the consumer reports exactly one edge, freshly Ready, with zero
// backlog. That leaves a real gap: a host suspension long enough to make the
// *next* edge itself appear Backlog (not merely Ready-but-unconsumed) while a
// VI transaction is genuinely still durably owned can never recover, because
// no proof condition covers it, regardless of what independent evidence a
// caller could supply. That ownership may be either AwaitViRfi or the earlier
// TokenPending phase with one live, unconsumed renderer token. This predicate
// closes exactly that gap using the SAME structural ownership checks as the
// Ready case, but permits the observation to be Backlog provided:
//   - it still names the boundary's edge's *direct* successor (sequence and
//     deadline both advance by exactly one period past the owned edge --
//     never more), so recovery never claims more than the one new edge that
//     the boundary's own state proves must be next, and
//   - the boundary's ownership/identity checks are unchanged from the Ready
//     case (same phase, same durable-transaction fields, same ordering of
//     last_guest_ticks against both the owned edge and the successor).
// This predicate alone is NOT sufficient evidence of a host suspension --
// exactly like active_boundary_owns_ready_vi_successor, callers must
// separately prove the interval via is_host_suspension_candidate_interval
// (the same wall-time-vs-CPU-time evidence already required for every other
// recovery path) before treating this as recoverable.
[[nodiscard]] constexpr bool active_boundary_owns_successor_during_backlog(
    const BoundarySnapshot& boundary,
    const timing::PeriodicDeadlineObservation& observation,
    std::uint64_t last_guest_ticks) noexcept {
    const bool token_pending =
        boundary.phase == BoundaryPhase::TokenPending &&
        static_cast<bool>(boundary.token) && !boundary.token_consumed;
    const bool awaiting_vi_rfi = boundary.phase == BoundaryPhase::AwaitViRfi;
    if ((!token_pending && !awaiting_vi_rfi) ||
        boundary.serial == 0u || boundary.vi_rfi_seen ||
        boundary.edge.kind != timing::EventKind::VideoInterface ||
        boundary.edge.replaceable || boundary.edge.sequence == 0u ||
        boundary.edge.deadline_ticks == 0u ||
        boundary.interrupted_resume_pc == 0u ||
        boundary.interrupted_context == 0u ||
        observation.status != timing::PeriodicConsumeStatus::Backlog ||
        observation.expected_sequence == 0u ||
        observation.expected_deadline_ticks == 0u ||
        last_guest_ticks < boundary.edge.deadline_ticks ||
        last_guest_ticks >= observation.expected_deadline_ticks ||
        boundary.edge.sequence ==
            std::numeric_limits<std::uint64_t>::max() ||
        boundary.edge.deadline_ticks >
            std::numeric_limits<std::uint64_t>::max() -
                timing::kViPeriodTicks) {
        return false;
    }
    return boundary.edge.sequence + 1u == observation.expected_sequence &&
        boundary.edge.deadline_ticks + timing::kViPeriodTicks ==
            observation.expected_deadline_ticks;
}

// Durable ownership for one consumed VI deadline. The state deliberately
// contains no host stack references: translated exception entry ends in RFI
// and unwinds every native caller before the guest scheduler target resumes.
class PendingBoundary {
public:
    [[nodiscard]] bool capture(BoundaryCapture&& capture) {
        if (snapshot_.phase != BoundaryPhase::Empty || capture.serial == 0u ||
            capture.edge.kind != timing::EventKind::VideoInterface ||
            capture.edge.sequence == 0u || capture.edge.deadline_ticks == 0u ||
            capture.interrupted_resume_pc == 0u ||
            capture.interrupted_context == 0u) {
            return false;
        }
        snapshot_.serial = capture.serial;
        snapshot_.phase = BoundaryPhase::Captured;
        snapshot_.edge = capture.edge;
        snapshot_.interrupted_resume_pc = capture.interrupted_resume_pc;
        snapshot_.interrupted_context = capture.interrupted_context;
        captured_fifo_ = std::move(capture.fifo);
        snapshot_.rendered_fifo_size = captured_fifo_.size();
        snapshot_.use_realtime_vi = capture.use_realtime_vi;
        snapshot_.present_this_vi = capture.present_this_vi;
        snapshot_.present_in_render_frame = capture.present_in_render_frame;
        snapshot_.render_completion_independent =
            capture.render_completion_independent;
        snapshot_.xfb_copies_before_render =
            capture.xfb_copies_before_render;
        snapshot_.started = capture.started;
        return true;
    }

    [[nodiscard]] bool mark_submit_pending() noexcept {
        if (snapshot_.phase != BoundaryPhase::Captured) {
            return false;
        }
        snapshot_.phase = BoundaryPhase::SubmitPending;
        return true;
    }

    // A WouldBlock submit leaves immutable captured bytes and every identity
    // field untouched so the exact same work can be retried later.
    [[nodiscard]] bool mark_submit_would_block() const noexcept {
        return snapshot_.phase == BoundaryPhase::SubmitPending;
    }

    [[nodiscard]] bool mark_submitted(
        gx::FramePeCompletionToken token,
        bool rendered_retrace_frame) noexcept {
        if (snapshot_.phase != BoundaryPhase::Captured &&
            snapshot_.phase != BoundaryPhase::SubmitPending) {
            return false;
        }
        if (!captured_fifo_.empty() && !token) {
            return false;
        }
        snapshot_.token = token;
        snapshot_.rendered_retrace_frame = rendered_retrace_frame;
        captured_fifo_.clear();
        if (token) {
            snapshot_.phase = BoundaryPhase::TokenPending;
        } else {
            snapshot_.token_consumed = true;
            snapshot_.phase = BoundaryPhase::TokenConsumed;
        }
        return true;
    }

    // wait_for_frame_pe_completion_for consumes the token before returning
    // true. Persist that fact before any later service can transfer control.
    [[nodiscard]] bool mark_token_consumed(
        gx::FramePeCompletionToken token) noexcept {
        if (snapshot_.phase != BoundaryPhase::TokenPending ||
            snapshot_.token != token || snapshot_.token_consumed) {
            return false;
        }
        snapshot_.token_consumed = true;
        snapshot_.phase = BoundaryPhase::TokenConsumed;
        return true;
    }

    // IRQ24 owns the deadline; the simulation-owned FIFO receipt owns the
    // later fence. Transfer, never forge or consume, the exact backend token.
    [[nodiscard]] bool detach_token_to_receipt(
        gx::FramePeCompletionToken token,
        std::uint64_t receipt_serial) noexcept {
        if (!snapshot_.render_completion_independent ||
            snapshot_.phase != BoundaryPhase::TokenPending ||
            snapshot_.token != token || !token || snapshot_.token_consumed ||
            snapshot_.detached_receipt_serial != 0u || receipt_serial == 0u) {
            return false;
        }
        snapshot_.detached_receipt_serial = receipt_serial;
        snapshot_.phase = BoundaryPhase::PeDrain;
        return true;
    }

    [[nodiscard]] bool begin_pe_drain() noexcept {
        if (snapshot_.phase != BoundaryPhase::TokenConsumed ||
            !snapshot_.token_consumed) {
            return false;
        }
        snapshot_.phase = BoundaryPhase::PeDrain;
        return true;
    }

    // The first rendered XFB may become visible only after the frame token is
    // consumed. Presentation may be promoted at that exact boundary, but never
    // while submission is mutable or after VI has already been latched.
    [[nodiscard]] bool promote_present_this_vi() noexcept {
        if (snapshot_.phase != BoundaryPhase::TokenConsumed ||
            !snapshot_.token_consumed) {
            return false;
        }
        snapshot_.present_this_vi = true;
        return true;
    }

    [[nodiscard]] bool mark_vi_level_latched() noexcept {
        if (snapshot_.phase != BoundaryPhase::PeDrain) {
            return false;
        }
        snapshot_.phase = BoundaryPhase::ViLevelLatched;
        return true;
    }

    [[nodiscard]] bool await_vi_rfi() noexcept {
        if (snapshot_.phase != BoundaryPhase::ViLevelLatched) {
            return false;
        }
        snapshot_.phase = BoundaryPhase::AwaitViRfi;
        return true;
    }

    [[nodiscard]] bool confirm_vi_rfi(
        std::uint64_t owner_serial,
        std::uint32_t selected_interrupt) noexcept {
        constexpr std::uint32_t kViInterrupt = 24u;
        if (snapshot_.phase != BoundaryPhase::AwaitViRfi ||
            owner_serial != snapshot_.serial ||
            selected_interrupt != kViInterrupt || snapshot_.vi_rfi_seen) {
            return false;
        }
        snapshot_.vi_rfi_seen = true;
        snapshot_.phase = BoundaryPhase::Finalize;
        return true;
    }

    [[nodiscard]] bool set_xfb_copies_after_present(
        std::uint64_t count) noexcept {
        if (snapshot_.phase != BoundaryPhase::Finalize) {
            return false;
        }
        snapshot_.xfb_copies_after_present = count;
        return true;
    }

    [[nodiscard]] bool finish(BoundarySnapshot& completed) noexcept {
        if (snapshot_.phase != BoundaryPhase::Finalize ||
            !snapshot_.vi_rfi_seen) {
            return false;
        }
        completed = snapshot_;
        snapshot_ = {};
        captured_fifo_.clear();
        return true;
    }

    [[nodiscard]] bool blocks_context(
        std::uint32_t context) const noexcept {
        return context != 0u && context == snapshot_.interrupted_context &&
            (snapshot_.phase == BoundaryPhase::Captured ||
             snapshot_.phase == BoundaryPhase::SubmitPending ||
             snapshot_.phase == BoundaryPhase::TokenPending);
    }

    [[nodiscard]] bool active() const noexcept {
        return snapshot_.phase != BoundaryPhase::Empty;
    }
    [[nodiscard]] const BoundarySnapshot& snapshot() const noexcept {
        return snapshot_;
    }
    [[nodiscard]] const std::vector<std::byte>& captured_fifo() const noexcept {
        return captured_fifo_;
    }

private:
    BoundarySnapshot snapshot_{};
    std::vector<std::byte> captured_fifo_{};
};

}  // namespace galaxy::vi
