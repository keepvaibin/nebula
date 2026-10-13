#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <utility>

namespace galaxy::host {

inline constexpr std::size_t kHostPointerTransitionCapacity = 1024u;

// Cache lifetime uses this precise clock only. Acquisition and Win32 message timestamps stay on
// the millisecond epoch and must not be passed here. The exclusive boundary expires a 1 ms cache
// at exactly 1 ms. Invalid or future anchors are rejected before unsigned subtraction, and the
// result is capped before multiplication.
[[nodiscard]] constexpr bool host_pointer_poll_cache_fresh(
    std::uint64_t now_steady_ns,
    std::uint64_t cached_steady_ns,
    std::uint64_t configured_cache_ms) noexcept {
    const std::uint64_t cache_ms =
        configured_cache_ms > 16u ? 16u : configured_cache_ms;
    return cache_ms != 0u && cached_steady_ns != 0u &&
        now_steady_ns >= cached_steady_ns &&
        now_steady_ns - cached_steady_ns < cache_ms * 1'000'000u;
}

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

// One coherent publication of the absolute-pointer state read by the native HID adapter.
// Coordinates, visibility, sequence counters and sample time come from the same producer update.
// Reading them separately can mix parts of different mouse messages.
struct HostPointerSnapshot {
    HostPointerCoordinates coordinates{};
    std::uint64_t absolute_sequence = 0;
    std::uint64_t event_sequence = 0;
    // Acquisition time for the coordinates. Cursor polling may advance it, but must never make an old
    // Win32 client message look recent.
    std::uint64_t absolute_acquired_ms = 0;
    // Authority clock, advanced only by WM_MOUSEMOVE and button messages that carry client coordinates.
    // Cursor polling leaves it unchanged.
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

// IR coordinate ownership is geometric, not focus-based. A known outside coordinate is an explicit
// sensor-bar loss. Only an invalid coordinate uses the bounded uncertainty hold. Focus still governs
// keyboard and button ownership and OS-cursor visibility, but must not stop global cursor tracking
// over the game client.
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

// Mouse buttons belong to the visible game client under the cursor, not to the focused window.
// Clicking an unfocused game window must still reach the Wii Remote A/B report. An overlapping app
// or a known outside coordinate must not inject a game click.
[[nodiscard]] constexpr bool select_host_pointer_mouse_button_delivery(
    bool,
    bool absolute_valid,
    bool inside_client,
    bool projected_visible,
    bool game_window_under_cursor) noexcept {
    return absolute_valid && inside_client && projected_visible &&
        game_window_under_cursor;
}

// Pure cursor-poll selection policy. The baseline is the last poll whose snapshot publication
// succeeded. A rejected candidate is not installed as the baseline, so an unchanged candidate can
// be reconsidered after the client-message authority/stale-center timeout expires.
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

// Local GetCursorPos sampling is independent of the render thread's message pump and stays
// authoritative at the native HID clock. The exception is RDP: some hosts return a stale centered
// GetCursorPos while WM_MOUSEMOVE carries the live coordinate. Adaptive mode therefore keeps the
// message authoritative only while it is recent. Forced mode trusts polling even in a remote session.
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

// Seeding client geometry does not acquire a real cursor coordinate. A forced center for a new
// renderer window starts inactive. Only a non-forced geometry refresh keeps an earlier
// inside-client state.
[[nodiscard]] constexpr bool
select_host_pointer_seed_inside(bool previously_inside_client,
                                bool force_center) noexcept {
    return previously_inside_client && !force_center;
}

struct HostPointerSeedPublicationInput {
    std::int32_t client_width = 0;
    std::int32_t client_height = 0;
    bool force_center = false;
    // Legacy caller field; geometry seeding does not use this as sample time.
    std::uint64_t acquired_ms = 0;
};

// Publishes client geometry without treating the synthetic center as a visible cursor. The absolute
// identity advances only when the renderer's seed or resize path reports a geometry change.
void publish_host_pointer_seed(
    HostPointerSnapshot& current,
    const HostPointerSeedPublicationInput& input) noexcept;

// A same-size window move changes ScreenToClient's coordinate epoch without
// acquiring a cursor or moving the retained point. Reject older in-flight polls.
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

// Publishes an authoritative Win32 visibility loss (WM_MOUSELEAVE, focus loss or capture
// cancellation). Advancing event_sequence makes an older cursor poll fail its compare-before-publish
// check, so it cannot revive the pointer after the newer event.
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

// Applies a selected inside-client sample only while the cached event and geometry used to classify
// it are still current. Each accepted absolute acquisition advances both identities, including
// outside-to-inside recovery at an unchanged coordinate. absolute_acquired_ms therefore always
// matches the absolute_sequence returned with it.
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

// First-acquisition fallback, used before a coordinate-bearing event has filled the cached geometry.
// Both observed identities are used for revalidation, and an accepted acquisition advances both.
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
// still match the snapshot used to classify it. A slow GetCursorPos or ScreenToClient call therefore
// cannot overwrite a newer WndProc coordinate or resize.
[[nodiscard]] HostPointerCursorOutsidePublicationResult
apply_host_pointer_cursor_outside_publication(
    HostPointerSnapshot& current,
    const HostPointerCursorOutsidePublicationInput& input) noexcept;

struct HostPointerCaptureCleanupDecision {
    bool publish_cancel_all = false;
    bool clear_window_state = false;
};

// ReleaseCapture sends WM_CAPTURECHANGED after a normal final button-up. That must not turn a pointer
// still inside the client into a synthetic leave. WM_CANCELMODE, and capture loss while a button is
// held, remain cleanup events.
[[nodiscard]] HostPointerCaptureCleanupDecision
select_host_pointer_capture_cleanup(
    bool cancel_mode,
    bool button_pressed) noexcept;

// A small multi-writer transactional cache. Mutations run under the same lock as snapshot(), so
// read-modify-write publications cannot lose sequence increments, and readers never see a torn
// tuple. Mutators must not call back into this cache.
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
// the event after it. A reader more than kHostPointerTransitionCapacity events behind gets the
// Overflow result, which is fatal to the caller, because silent resynchronization would lose button
// edges.
class HostPointerEventRing {
public:
    HostPointerEventRing() = default;
    ~HostPointerEventRing() = default;

    HostPointerEventRing(const HostPointerEventRing&) = delete;
    HostPointerEventRing& operator=(const HostPointerEventRing&) = delete;
    HostPointerEventRing(HostPointerEventRing&&) = delete;
    HostPointerEventRing& operator=(HostPointerEventRing&&) = delete;

    // Returns true only when the logical level changes and a transition is recorded. Repeated down or up
    // messages are ignored and leave the sequence unchanged.
    bool accept_button_transition(
        HostPointerButton button,
        bool down,
        const HostPointerCoordinates& coordinates,
        std::uint64_t absolute_acquired_ms) noexcept;

    // Both invalidation paths start a new input epoch, release both logical buttons, and publish one
    // CancelAll transition in that epoch.
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
