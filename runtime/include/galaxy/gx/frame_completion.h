#pragma once

// Per-frame completion tokens for submitted GX FIFO captures and the state machine that orders them.

#include "galaxy/frame_cadence_diagnostics.h"

#include <cstdint>
#include <deque>

namespace galaxy::gx {

// Longest wait for a frame's GPU fence before the renderer reports a hang.
inline constexpr std::uint32_t kFramePeCompletionTimeoutMs = 30'000u;

// Identifies one non-empty GX FIFO capture; held by the simulation thread. A capture without PE events is
// ready without a render wait; one with PE events completes when the render thread has seen its GPU fence and
// run its PE callbacks.
struct FramePeCompletionToken {
    // `epoch` advances on each contract reset and `value` within an epoch, so a token kept across a renderer
    // shutdown and reinitialize never matches a new frame.
    std::uint64_t epoch = 0;
    std::uint64_t value = 0;
    // EventFree or EventBearing, by the issuing call; read only by diagnostics (frame cadence, timeline).
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

// Per-frame PE completion state machine. Callers synchronize. Free of Win32 and D3D12 so it can be unit tested.
//
// A frame with no PE event is issued ready, since the game cannot observe when it finishes. A frame with PE
// events resolves to a strictly newer GPU fence (or on submission). Tokens complete in issue order, and each is
// waited on and consumed once, in the same order. Any misuse throws std::logic_error.
class FramePeCompletionContract {
public:
    [[nodiscard]] FramePeCompletionToken issue();
    // A token for a FIFO capture the preclassifier found free of PE FINISH and TOKEN events. It is ready at
    // once unless an older token is still pending.
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
    // Throws unless every issued token has completed and been consumed. reset() calls it: a token still
    // pending at shutdown is an error, not state to discard.
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
