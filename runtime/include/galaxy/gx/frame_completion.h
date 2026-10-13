#pragma once

#include "galaxy/frame_cadence_diagnostics.h"

#include <cstdint>
#include <deque>

namespace galaxy::gx {

inline constexpr std::uint32_t kFramePeCompletionTimeoutMs = 30'000u;
// Runtime waits are cooperative: one short native wait, then an AI/input deadline service
// pass. First-use pipeline builds on a valid scene can take several seconds, so the simulation
// owner allows the same completion budget as the backend rather than aborting a frame that is
// still progressing. Slices stay short. A wedged renderer still fails at the absolute backend
// deadline without replacing the owned result.
inline constexpr std::uint32_t kFramePeCooperativeWaitSliceMs = 1u;
inline constexpr std::uint32_t kFramePeCooperativeWaitTimeoutMs =
    kFramePeCompletionTimeoutMs;

struct FramePeCooperativeWaitStep {
    std::uint32_t wait_ms = 0;
    bool timed_out = false;
};

// Pure policy helper for the cooperative wait loop. Tests inject elapsed time directly, which
// keeps boundary and overflow behaviour deterministic.
[[nodiscard]] FramePeCooperativeWaitStep frame_pe_cooperative_wait_step(
    std::uint64_t elapsed_ms,
    std::uint32_t timeout_ms = kFramePeCooperativeWaitTimeoutMs,
    std::uint32_t slice_ms = kFramePeCooperativeWaitSliceMs);

// One cooperative PE-wait service slice. The native DSP MRAM rendezvous runs first: its worker
// is blocked until the CPU-owned guest-memory span is committed. AI then advances without a
// guest context. The PE wait already owns an undelivered VI edge, and a nested guest interrupt
// handler here could abandon that frame's completion token.
//
// Header-only, so the production ordering can be tested with the real DSP transaction boundary
// without exposing RuntimeState.
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

// Monotonic identity for one non-empty GX FIFO capture, owned by the simulation thread. An
// event-free receipt, preclassified, is ready without a render wait. A PE-bearing token is
// published by the render thread after that capture's native fence and callbacks complete.
struct FramePeCompletionToken {
    // `epoch` changes on every successful contract reset. `value` is monotonic only within an epoch.
    // Both are needed for identity, so a token kept across renderer shutdown and reinitialize never
    // aliases a new frame.
    std::uint64_t epoch = 0;
    std::uint64_t value = 0;
    // Classification comes from the exact FIFO preclassifier. It is diagnostic only; completion order
    // and readiness stay with FramePeCompletionContract.
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

// Thread-agnostic state machine for frame-specific PE completion. Callers own synchronization. It does not
// depend on Win32 or D3D12, so fence order and token conservation are testable.
//
// Resolution is FIFO-exact. A frame shown to be event-free by the FIFO preclassifier is issued ready, since it
// has no guest-visible PE boundary. A frame with PE callbacks binds to one strictly newer native GPU fence.
// Completion advances only from the front, and each wait is consumed once, in issuance order.
class FramePeCompletionContract {
public:
    [[nodiscard]] FramePeCompletionToken issue();
    // Issues a receipt for a FIFO capture that an exact parser proved has no PE FINISH or TOKEN event.
    // The receipt is ready at once unless an older PE-bearing token still blocks the frontier.
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
    // Proves internal conservation and a fully consumed frontier. This is the shutdown and reinitialize
    // gate: pending or unconsumed tokens are errors, never state a reset may erase.
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
