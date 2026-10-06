#pragma once

#include "galaxy/frame_cadence_diagnostics.h"

#include <cstdint>
#include <deque>

namespace galaxy::gx {

inline constexpr std::uint32_t kFramePeCompletionTimeoutMs = 30'000u;
// Runtime waits are cooperative: one short native wait is followed by an
// AI/input deadline service pass. The overall runtime budget is deliberately
// much smaller than the low-level 30-second diagnostic fence timeout so a
// wedged renderer hard-fails instead of freezing gameplay for half a minute.
inline constexpr std::uint32_t kFramePeCooperativeWaitSliceMs = 1u;
inline constexpr std::uint32_t kFramePeCooperativeWaitTimeoutMs = 2'000u;

struct FramePeCooperativeWaitStep {
    std::uint32_t wait_ms = 0;
    bool timed_out = false;
};

// Pure policy helper used by the runtime cooperative wait loop. Tests inject
// elapsed time directly, keeping boundary/overflow behavior deterministic.
[[nodiscard]] FramePeCooperativeWaitStep frame_pe_cooperative_wait_step(
    std::uint64_t elapsed_ms,
    std::uint32_t timeout_ms = kFramePeCooperativeWaitTimeoutMs,
    std::uint32_t slice_ms = kFramePeCooperativeWaitSliceMs);

// One cooperative PE-wait service slice. The native DSP MRAM rendezvous must
// run first because its worker is synchronously blocked until the CPU-owned
// guest-memory span is committed. AI then advances without a guest context:
// the PE wait already owns an undelivered VI edge, so entering a nested guest
// interrupt handler here could abandon that frame's completion token.
//
// This is header-only so the production ordering can be exercised with the
// real DSP transaction boundary in a deterministic unit test without exposing
// RuntimeState or making the runtime executable test-only aware.
template <
    typename ServiceNativeDspWake,
    typename ThrowIfNativeAudioFailed,
    typename ServiceAiDma,
    typename ServiceNativeInput,
    typename HasPendingAiDeadline>
void service_frame_pe_realtime_device_slice(
    ServiceNativeDspWake&& service_native_dsp_wake,
    ThrowIfNativeAudioFailed&& throw_if_native_audio_failed,
    ServiceAiDma&& service_ai_dma,
    ServiceNativeInput&& service_native_input,
    HasPendingAiDeadline&& has_pending_ai_deadline) {
    service_native_dsp_wake();
    throw_if_native_audio_failed();
    service_ai_dma(nullptr);
    service_native_input();
    if (has_pending_ai_deadline()) {
        service_ai_dma(nullptr);
    }
    throw_if_native_audio_failed();
}

// Monotonic identity for one non-empty GX FIFO capture. The token is owned by
// the simulation thread. A preclassified event-free receipt is ready without
// a render wait; a PE-bearing token is published by the render thread only
// after that capture's exact native fence and callbacks complete.
struct FramePeCompletionToken {
    // `epoch` changes on every successful contract reset. `value` is only
    // monotonic within that epoch; both fields are required identity so a
    // token retained across renderer shutdown/reinitialize can never alias a
    // newly issued frame.
    std::uint64_t epoch = 0;
    std::uint64_t value = 0;
    // Classification comes from the exact FIFO preclassifier. It is
    // diagnostic identity only; completion order and readiness remain owned
    // by FramePeCompletionContract.
    cadence::FrameTokenKind kind = cadence::FrameTokenKind::None;

    [[nodiscard]] explicit operator bool() const noexcept {
        return epoch != 0u && value != 0u;
    }
    [[nodiscard]] bool operator==(
        const FramePeCompletionToken&) const noexcept = default;
};

enum class FramePeWaitState : std::uint8_t {
    Pending,
    Complete,
};

struct FramePeCompletionStats {
    std::uint64_t epoch = 0;
    std::uint64_t issued = 0;
    std::uint64_t resolved = 0;
    std::uint64_t completed = 0;
    std::uint64_t consumed = 0;
    std::uint64_t unresolved = 0;
    std::uint64_t submission_ready = 0;
    std::uint64_t fence_pending = 0;
};

// Thread-agnostic state machine for frame-specific PE completion. Callers own
// synchronization; keeping the contract independent of Win32/D3D12 makes all
// fence order and token conservation cases deterministically testable.
//
// Resolution is FIFO-exact. A frame proven event-free by the independent FIFO
// preclassifier may be issued already ready because it has no guest-visible PE
// boundary to wait for; a frame carrying PE callbacks binds to one strictly
// newer native GPU fence. Completion may only advance from the front, and
// waits must be consumed exactly once in issuance order.
class FramePeCompletionContract {
public:
    [[nodiscard]] FramePeCompletionToken issue();
    // Issues a valid receipt for a FIFO capture that an exact parser proved
    // contains no PE FINISH/TOKEN event. The receipt is immediately ready
    // unless an older PE-bearing token still blocks the ordered frontier.
    [[nodiscard]] FramePeCompletionToken issue_event_free();
    void resolve_on_submission(FramePeCompletionToken token);
    void resolve_to_fence(
        FramePeCompletionToken token,
        std::uint64_t fence_value);
    [[nodiscard]] bool observe_completed_fence(
        std::uint64_t completed_fence_value);

    [[nodiscard]] FramePeWaitState probe_wait(
        FramePeCompletionToken token) const;
    void consume_wait(FramePeCompletionToken token);

    [[nodiscard]] FramePeCompletionStats stats() const noexcept;
    // Proves both internal conservation and a fully consumed frontier. This is
    // the shutdown/reinitialize gate: pending or merely-unconsumed tokens are
    // errors, never state that reset is allowed to erase.
    void audit_drained() const;
    void reset();

private:
    enum class Resolution : std::uint8_t {
        Unresolved,
        Submission,
        Fence,
    };

    struct Entry {
        FramePeCompletionToken token{};
        Resolution resolution = Resolution::Unresolved;
        std::uint64_t fence_value = 0;
    };

    [[nodiscard]] Entry& next_resolution_entry(
        FramePeCompletionToken token);
    [[nodiscard]] bool advance_completed();
    void validate_wait_token(FramePeCompletionToken token) const;

    std::deque<Entry> entries_;
    std::uint64_t epoch_ = 0;
    std::uint64_t issued_ = 0;
    std::uint64_t resolved_ = 0;
    std::uint64_t completed_ = 0;
    std::uint64_t consumed_ = 0;
    std::uint64_t last_bound_fence_ = 0;
    std::uint64_t observed_completed_fence_ = 0;
};

}  // namespace galaxy::gx
