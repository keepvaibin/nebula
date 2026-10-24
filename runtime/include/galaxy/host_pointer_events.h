#pragma once

// The Windows mouse pointer as the Wii Remote pointer sees it: the coordinate
// snapshot shared by the window procedure and cursor polling, the rules that
// decide which sample wins, and the journal of button transitions.

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <utility>

namespace galaxy::host {

inline constexpr std::size_t kHostPointerTransitionCapacity = 1024u;

enum class HostPointerButton : std::uint8_t {
    Left,
    Right,
};

enum class HostPointerTransitionKind : std::uint8_t {
    LeftDown,
    LeftUp,
    RightDown,
    RightUp,
    CancelAll,
};

struct HostPointerCoordinates {
    bool absolute_valid = false;
    bool inside_client = false;
    std::int32_t client_x = 0;
    std::int32_t client_y = 0;
    std::int32_t client_width = 0;
    std::int32_t client_height = 0;
};

// The absolute pointer state the Wii Remote report is built from. Coordinates, visibility, sequence
// counters and sample time are published together; reading them separately can mix parts of
// different mouse messages.
struct HostPointerSnapshot {
    HostPointerCoordinates coordinates{};
    std::uint64_t absolute_sequence = 0;
    std::uint64_t event_sequence = 0;
    // Acquisition time of the coordinates. Cursor polling may advance it, but never makes an old
    // Win32 client message look recent.
    std::uint64_t absolute_acquired_ms = 0;
    // Advanced only by WM_MOUSEMOVE and button messages that carry client coordinates; cursor
    // polling leaves it unchanged.
    std::uint64_t last_client_message_ms = 0;
    std::uint64_t leave_sequence = 0;
};

struct HostPointerClientMessageTiming {
    bool age_valid = false;
    std::uint64_t age_ms = 0;
    bool recent = false;
    bool stale_center_guard_active = false;
};

[[nodiscard]] HostPointerClientMessageTiming
classify_host_pointer_client_message_timing(
    std::uint64_t now_ms,
    std::uint64_t last_client_message_ms,
    std::uint64_t recent_threshold_ms,
    std::uint64_t stale_center_threshold_ms) noexcept;

struct HostPointerIrAvailabilityDecision {
    bool acquired_inside_available = false;
    bool invalid_sample_hold_allowed = false;
};

// IR availability follows geometry, not focus: a known outside coordinate is a sensor-bar loss, and
// only an invalid coordinate may use the bounded hold. Focus governs keyboard and button ownership
// and OS-cursor visibility, but cursor tracking over the game client continues without it. The
// first parameter (focus) is unused.
[[nodiscard]] constexpr HostPointerIrAvailabilityDecision
select_host_pointer_ir_availability(
    bool,
    bool absolute_valid,
    bool inside_client) noexcept {
    return {
        absolute_valid && inside_client,
        !absolute_valid,
    };
}

// Mouse buttons belong to the visible game client under the cursor, not to the focused window: a
// click on an unfocused game window reaches the Wii Remote A/B report, while an overlapping window
// or a known outside coordinate never produces a game click. The first parameter (focus) is unused.
[[nodiscard]] constexpr bool select_host_pointer_mouse_button_delivery(
    bool,
    bool absolute_valid,
    bool inside_client,
    bool projected_visible,
    bool game_window_under_cursor) noexcept {
    return absolute_valid && inside_client && projected_visible &&
        game_window_under_cursor;
}

// Whether a cursor poll publishes. The baseline is the last poll whose publication succeeded; a
// rejected candidate does not become the baseline, so an unchanged candidate is reconsidered once
// the client-message authority or stale-center timeout expires.
struct HostPointerCursorPollSelectionInput {
    bool accepted_baseline_valid = false;
    std::int32_t accepted_baseline_x = 0;
    std::int32_t accepted_baseline_y = 0;
    std::int32_t candidate_x = 0;
    std::int32_t candidate_y = 0;
    bool reject_as_stale_center = false;
    bool recovers_visibility = false;
    bool authoritative = false;
    // The accepted poll baseline is not necessarily the current publication: a client message can
    // replace it while the OS cursor stays still.
    bool current_publication_valid = false;
    std::int32_t current_publication_x = 0;
    std::int32_t current_publication_y = 0;
};

struct HostPointerCursorPollSelection {
    bool candidate_changed = false;
    bool should_publish = false;
};

[[nodiscard]] HostPointerCursorPollSelection select_host_pointer_cursor_poll(
    const HostPointerCursorPollSelectionInput& input) noexcept;

enum class HostPointerCursorPollMode : std::uint8_t {
    Disabled,
    Adaptive,
    Forced,
};

struct HostPointerCursorPollAuthorityDecision {
    bool poll_enabled = false;
    bool authoritative = false;
    bool stale_center_guard_allowed = false;
};

// GetCursorPos sampling does not depend on the render thread's message pump and is authoritative at
// the Wii Remote report rate. The exception is RDP: some hosts return a stale centred GetCursorPos
// while WM_MOUSEMOVE carries the live coordinate, so in a remote session Adaptive mode lets a recent
// message take precedence. Forced mode trusts polling even in a remote session.
[[nodiscard]] constexpr HostPointerCursorPollAuthorityDecision
select_host_pointer_cursor_poll_authority(
    HostPointerCursorPollMode mode,
    bool remote_session,
    bool recent_client_message) noexcept {
    if (mode == HostPointerCursorPollMode::Disabled) {
        return {};
    }
    const bool remote_message_first =
        mode == HostPointerCursorPollMode::Adaptive && remote_session;
    return {
        true,
        !remote_message_first || !recent_client_message,
        remote_message_first,
    };
}

// Seeding client geometry does not acquire a cursor coordinate: a forced centre for a new window
// starts outside, and only a non-forced geometry refresh keeps an earlier inside-client state.
[[nodiscard]] constexpr bool
select_host_pointer_seed_inside(bool previously_inside_client,
                                bool force_center) noexcept {
    return previously_inside_client && !force_center;
}

struct HostPointerSeedPublicationInput {
    std::int32_t client_width = 0;
    std::int32_t client_height = 0;
    bool force_center = false;
    // Not read: seeding is not a cursor sample.
    std::uint64_t acquired_ms = 0;
};

// Publishes client geometry without treating the seeded centre as a visible cursor. The absolute
// identity advances only when the seed or a resize changes the geometry.
void publish_host_pointer_seed(
    HostPointerSnapshot& current,
    const HostPointerSeedPublicationInput& input) noexcept;

// A same-size window move changes what ScreenToClient returns without moving the
// retained point; advancing the absolute identity rejects polls already in flight.
void invalidate_host_pointer_geometry(HostPointerSnapshot& current) noexcept;

struct HostPointerClientCoordinatePublicationInput {
    std::int32_t raw_client_x = 0;
    std::int32_t raw_client_y = 0;
    std::int32_t client_width = 0;
    std::int32_t client_height = 0;
    std::uint64_t acquired_ms = 0;
};

// Publishes a coordinate-bearing Win32 client message. Event and absolute
// identities advance together; an outside message records the new event as
// the leave identity.
void publish_host_pointer_client_coordinate(
    HostPointerSnapshot& current,
    const HostPointerClientCoordinatePublicationInput& input) noexcept;

// Publishes a Win32 visibility loss (WM_MOUSELEAVE, focus loss or capture cancellation). Advancing
// event_sequence makes an older cursor poll fail its compare-before-publish check, so it cannot
// revive the pointer after the newer event.
void publish_host_pointer_window_visibility_loss(
    HostPointerSnapshot& current) noexcept;

struct HostPointerCursorInsidePublicationInput {
    bool selected_for_publication = false;
    std::int32_t sample_x = 0;
    std::int32_t sample_y = 0;
    std::uint64_t sample_acquired_ms = 0;
    std::uint64_t observed_event_sequence = 0;
    std::int32_t observed_client_width = 0;
    std::int32_t observed_client_height = 0;
    std::uint64_t observed_absolute_sequence = 0;
};

enum class HostPointerCursorInsidePublicationResult : std::uint8_t {
    Rejected,
    PublishedInside,
};

// Applies a selected inside-client sample only while the event and geometry it was classified
// against are still current. Each accepted acquisition advances both identities, including
// outside-to-inside recovery at an unchanged coordinate, so absolute_acquired_ms always matches
// the absolute_sequence returned with it.
[[nodiscard]] HostPointerCursorInsidePublicationResult
apply_host_pointer_cursor_inside_publication(
    HostPointerSnapshot& current,
    const HostPointerCursorInsidePublicationInput& input) noexcept;

struct HostPointerFallbackInsidePublicationInput {
    std::int32_t sample_x = 0;
    std::int32_t sample_y = 0;
    std::int32_t client_width = 0;
    std::int32_t client_height = 0;
    std::uint64_t sample_acquired_ms = 0;
    std::uint64_t observed_event_sequence = 0;
    std::uint64_t observed_absolute_sequence = 0;
};

enum class HostPointerFallbackInsidePublicationResult : std::uint8_t {
    Rejected,
    PublishedInside,
};

// The first acquisition, before a coordinate-bearing message has filled the geometry. Both observed
// identities are revalidated, and an accepted acquisition advances both.
[[nodiscard]] HostPointerFallbackInsidePublicationResult
apply_host_pointer_fallback_inside_publication(
    HostPointerSnapshot& current,
    const HostPointerFallbackInsidePublicationInput& input) noexcept;

struct HostPointerCursorOutsidePublicationInput {
    bool sample_succeeded = false;
    bool sample_inside_client = false;
    bool authoritative = false;
    std::uint64_t observed_event_sequence = 0;
    std::int32_t observed_client_width = 0;
    std::int32_t observed_client_height = 0;
    std::uint64_t observed_absolute_sequence = 0;
};

enum class HostPointerCursorOutsidePublicationResult : std::uint8_t {
    Rejected,
    AlreadyOutside,
    PublishedOutside,
};

// Applies an authoritative outside-client observation only if the message sequence and client bounds
// still match the snapshot it was classified against, so a slow GetCursorPos or ScreenToClient call
// cannot overwrite a newer window-procedure coordinate or resize.
[[nodiscard]] HostPointerCursorOutsidePublicationResult
apply_host_pointer_cursor_outside_publication(
    HostPointerSnapshot& current,
    const HostPointerCursorOutsidePublicationInput& input) noexcept;

struct HostPointerCaptureCleanupDecision {
    bool publish_cancel_all = false;
    bool clear_window_state = false;
};

// ReleaseCapture sends WM_CAPTURECHANGED after a normal final button-up; that does not turn a pointer
// still inside the client into a leave. WM_CANCELMODE, and capture loss while a button is held, are
// cleanup events.
[[nodiscard]] HostPointerCaptureCleanupDecision
select_host_pointer_capture_cleanup(
    bool cancel_mode,
    bool button_pressed) noexcept;

// The snapshot behind one lock shared by every writer and reader: read-modify-write publications
// cannot lose sequence increments, and readers never see a torn snapshot. Mutations must not call
// back into the cache.
class HostPointerSnapshotCache {
public:
    HostPointerSnapshotCache() = default;
    ~HostPointerSnapshotCache() = default;

    HostPointerSnapshotCache(const HostPointerSnapshotCache&) = delete;
    HostPointerSnapshotCache& operator=(
        const HostPointerSnapshotCache&) = delete;
    HostPointerSnapshotCache(HostPointerSnapshotCache&&) = delete;
    HostPointerSnapshotCache& operator=(HostPointerSnapshotCache&&) = delete;

    [[nodiscard]] HostPointerSnapshot snapshot() const noexcept;

    template <typename Mutation>
    [[nodiscard]] HostPointerSnapshot update(Mutation&& mutation) noexcept {
        static_assert(
            std::is_nothrow_invocable_v<Mutation&, HostPointerSnapshot&>,
            "host-pointer snapshot mutations must be noexcept");
        const std::lock_guard<std::mutex> lock(mutex_);
        mutation(snapshot_);
        return snapshot_;
    }

private:
    mutable std::mutex mutex_;
    HostPointerSnapshot snapshot_{};
};

struct HostPointerTransition {
    std::uint64_t sequence = 0;
    std::uint64_t focus_epoch = 0;
    HostPointerTransitionKind kind = HostPointerTransitionKind::CancelAll;
    HostPointerCoordinates coordinates{};
    // GetTickCount64 milliseconds captured with this transition's absolute coordinates. A consumer
    // replaces the coordinates, sequence and this timestamp together.
    std::uint64_t absolute_acquired_ms = 0;
};

enum class HostPointerTransitionReadResult : std::uint8_t {
    Empty,
    Ready,
    Overflow,
};

struct HostPointerTransitionPoll {
    HostPointerTransitionReadResult result =
        HostPointerTransitionReadResult::Empty;
    std::uint64_t latest_sequence = 0;
    std::uint64_t current_focus_epoch = 1;
    HostPointerTransition transition{};
};

// Bounded, non-destructive transition journal. Each reader keeps its own sequence cursor and reads
// the event after it. A reader more than kHostPointerTransitionCapacity events behind gets Overflow,
// which the caller treats as fatal: resynchronizing silently would lose button edges.
class HostPointerEventRing {
public:
    HostPointerEventRing() = default;
    ~HostPointerEventRing() = default;

    HostPointerEventRing(const HostPointerEventRing&) = delete;
    HostPointerEventRing& operator=(const HostPointerEventRing&) = delete;
    HostPointerEventRing(HostPointerEventRing&&) = delete;
    HostPointerEventRing& operator=(HostPointerEventRing&&) = delete;

    // True when the button's level changed and a transition was recorded; a repeated down or up
    // message leaves the sequence unchanged.
    bool accept_button_transition(
        HostPointerButton button,
        bool down,
        const HostPointerCoordinates& coordinates,
        std::uint64_t absolute_acquired_ms) noexcept;

    // Both start a new focus epoch, release both buttons and publish one CancelAll transition in
    // that epoch.
    void invalidate_focus(
        const HostPointerCoordinates& coordinates,
        std::uint64_t absolute_acquired_ms) noexcept;
    void cancel_unexpected_capture(
        const HostPointerCoordinates& coordinates,
        std::uint64_t absolute_acquired_ms) noexcept;

    [[nodiscard]] HostPointerTransitionPoll read_after(
        std::uint64_t after_sequence) const noexcept;

private:
    void cancel_all_locked(
        const HostPointerCoordinates& coordinates,
        std::uint64_t absolute_acquired_ms) noexcept;
    void publish_locked(
        HostPointerTransitionKind kind,
        const HostPointerCoordinates& coordinates,
        std::uint64_t absolute_acquired_ms) noexcept;

    mutable std::mutex mutex_;
    std::array<
        HostPointerTransition,
        kHostPointerTransitionCapacity> transitions_{};
    std::uint64_t latest_sequence_ = 0;
    std::uint64_t focus_epoch_ = 1;
    bool left_down_ = false;
    bool right_down_ = false;
};

}  // namespace galaxy::host
