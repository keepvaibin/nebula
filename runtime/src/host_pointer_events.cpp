#include "galaxy/host_pointer_events.h"

#include <algorithm>
#include <exception>
#include <limits>

namespace galaxy::host {
namespace {

[[noreturn]] void hard_fail_pointer_event_invariant() noexcept {
    std::terminate();
}

void advance_snapshot_sequences_or_fail(
    HostPointerSnapshot& current,
    bool advance_event,
    bool advance_absolute) noexcept {
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    if ((advance_event && current.event_sequence == maximum) ||
        (advance_absolute && current.absolute_sequence == maximum)) {
        hard_fail_pointer_event_invariant();
    }
    if (advance_event) {
        ++current.event_sequence;
    }
    if (advance_absolute) {
        ++current.absolute_sequence;
    }
}

bool valid_client_extent(std::int32_t width, std::int32_t height) noexcept {
    return width > 1 && height > 1;
}

bool inside_client(std::int32_t x, std::int32_t y,
                   std::int32_t width, std::int32_t height) noexcept {
    return valid_client_extent(width, height) && x >= 0 && y >= 0 &&
           x < width && y < height;
}

HostPointerTransitionKind transition_kind(
    HostPointerButton button,
    bool down) noexcept {
    switch (button) {
    case HostPointerButton::Left:
        return down ? HostPointerTransitionKind::LeftDown
                    : HostPointerTransitionKind::LeftUp;
    case HostPointerButton::Right:
        return down ? HostPointerTransitionKind::RightDown
                    : HostPointerTransitionKind::RightUp;
    }
    hard_fail_pointer_event_invariant();
}

}  // namespace

HostPointerClientMessageTiming
classify_host_pointer_client_message_timing(
    std::uint64_t now_ms,
    std::uint64_t last_client_message_ms,
    std::uint64_t recent_threshold_ms,
    std::uint64_t stale_center_threshold_ms) noexcept {
    HostPointerClientMessageTiming timing{};
    timing.age_valid =
        last_client_message_ms != 0u && now_ms >= last_client_message_ms;
    if (!timing.age_valid) {
        return timing;
    }
    timing.age_ms = now_ms - last_client_message_ms;
    timing.recent = timing.age_ms <= recent_threshold_ms;
    timing.stale_center_guard_active =
        timing.age_ms <= stale_center_threshold_ms;
    return timing;
}

HostPointerCursorPollSelection select_host_pointer_cursor_poll(
    const HostPointerCursorPollSelectionInput& input) noexcept {
    const bool candidate_changed =
        !input.accepted_baseline_valid ||
        input.accepted_baseline_x != input.candidate_x ||
        input.accepted_baseline_y != input.candidate_y;
    const bool should_publish =
        !input.reject_as_stale_center &&
        (candidate_changed || input.recovers_visibility ||
         !input.current_publication_valid ||
         input.current_publication_x != input.candidate_x ||
         input.current_publication_y != input.candidate_y) &&
        input.authoritative;
    return {candidate_changed, should_publish};
}

void publish_host_pointer_seed(
    HostPointerSnapshot& current,
    const HostPointerSeedPublicationInput& input) noexcept {
    const int old_width = current.coordinates.client_width;
    const int old_height = current.coordinates.client_height;
    if (!valid_client_extent(input.client_width, input.client_height)) {
        if (current.coordinates.absolute_valid || old_width != 0 || old_height != 0) {
            advance_snapshot_sequences_or_fail(current, false, true);
        }
        current.coordinates = {};
        current.absolute_acquired_ms = 0;
        current.leave_sequence = current.event_sequence;
        return;
    }
    const bool initialized = old_width > 1 && old_height > 1;
    const int old_x = current.coordinates.client_x;
    const int old_y = current.coordinates.client_y;
    const bool old_inside = current.coordinates.inside_client;
    const int next_x = (input.force_center || !initialized)
        ? (input.client_width - 1) / 2
        : std::clamp(old_x, 0, input.client_width - 1);
    const int next_y = (input.force_center || !initialized)
        ? (input.client_height - 1) / 2
        : std::clamp(old_y, 0, input.client_height - 1);
    const bool next_inside = select_host_pointer_seed_inside(
        old_inside, input.force_center);
    const bool advance_absolute =
        !initialized || input.force_center || old_width != input.client_width ||
        old_height != input.client_height || old_x != next_x || old_y != next_y;
    advance_snapshot_sequences_or_fail(current, false, advance_absolute);

    current.coordinates = {
        true,
        next_inside,
        next_x,
        next_y,
        input.client_width,
        input.client_height,
    };
    // Geometry publication is not a cursor acquisition. Preserve the actual
    // prior sample's time when retaining it; a synthetic center has no sample.
    if (input.force_center || !initialized) {
        current.absolute_acquired_ms = 0;
    }
    if (!next_inside) {
        current.leave_sequence = current.event_sequence;
    }
}

void invalidate_host_pointer_geometry(HostPointerSnapshot& current) noexcept {
    advance_snapshot_sequences_or_fail(current, false, true);
}

void publish_host_pointer_client_coordinate(
    HostPointerSnapshot& current,
    const HostPointerClientCoordinatePublicationInput& input) noexcept {
    const bool valid = valid_client_extent(input.client_width, input.client_height);
    const bool inside = inside_client(input.raw_client_x, input.raw_client_y,
                                      input.client_width, input.client_height);
    advance_snapshot_sequences_or_fail(current, true, true);
    current.coordinates = valid ? HostPointerCoordinates{
        true,
        inside,
        std::clamp(input.raw_client_x, 0, input.client_width - 1),
        std::clamp(input.raw_client_y, 0, input.client_height - 1),
        input.client_width,
        input.client_height,
    } : HostPointerCoordinates{};
    current.absolute_acquired_ms = input.acquired_ms;
    current.last_client_message_ms = input.acquired_ms;
    if (!inside) {
        current.leave_sequence = current.event_sequence;
    }
}

void publish_host_pointer_window_visibility_loss(
    HostPointerSnapshot& current) noexcept {
    advance_snapshot_sequences_or_fail(current, true, false);
    current.coordinates.inside_client = false;
    current.leave_sequence = current.event_sequence;
}

HostPointerCursorInsidePublicationResult
apply_host_pointer_cursor_inside_publication(
    HostPointerSnapshot& current,
    const HostPointerCursorInsidePublicationInput& input) noexcept {
    if (!input.selected_for_publication ||
        current.event_sequence != input.observed_event_sequence ||
        current.absolute_sequence != input.observed_absolute_sequence ||
        !inside_client(input.sample_x, input.sample_y,
                       input.observed_client_width, input.observed_client_height) ||
        current.coordinates.client_width != input.observed_client_width ||
        current.coordinates.client_height != input.observed_client_height) {
        return HostPointerCursorInsidePublicationResult::Rejected;
    }
    advance_snapshot_sequences_or_fail(current, true, true);
    current.coordinates.client_x = input.sample_x;
    current.coordinates.client_y = input.sample_y;
    current.coordinates.inside_client = true;
    current.coordinates.absolute_valid = true;
    current.absolute_acquired_ms = input.sample_acquired_ms;
    return HostPointerCursorInsidePublicationResult::PublishedInside;
}

HostPointerFallbackInsidePublicationResult
apply_host_pointer_fallback_inside_publication(
    HostPointerSnapshot& current,
    const HostPointerFallbackInsidePublicationInput& input) noexcept {
    if (current.event_sequence != input.observed_event_sequence ||
        current.absolute_sequence != input.observed_absolute_sequence ||
        !inside_client(input.sample_x, input.sample_y,
                       input.client_width, input.client_height)) {
        return HostPointerFallbackInsidePublicationResult::Rejected;
    }
    advance_snapshot_sequences_or_fail(current, true, true);
    current.coordinates = {
        true,
        true,
        input.sample_x,
        input.sample_y,
        input.client_width,
        input.client_height,
    };
    current.absolute_acquired_ms = input.sample_acquired_ms;
    return HostPointerFallbackInsidePublicationResult::PublishedInside;
}

HostPointerCursorOutsidePublicationResult
apply_host_pointer_cursor_outside_publication(
    HostPointerSnapshot& current,
    const HostPointerCursorOutsidePublicationInput& input) noexcept {
    if (!input.sample_succeeded || input.sample_inside_client ||
        !input.authoritative ||
        current.event_sequence != input.observed_event_sequence ||
        current.absolute_sequence != input.observed_absolute_sequence ||
        !valid_client_extent(input.observed_client_width, input.observed_client_height) ||
        current.coordinates.client_width != input.observed_client_width ||
        current.coordinates.client_height != input.observed_client_height) {
        return HostPointerCursorOutsidePublicationResult::Rejected;
    }
    if (!current.coordinates.inside_client) {
        return HostPointerCursorOutsidePublicationResult::AlreadyOutside;
    }
    advance_snapshot_sequences_or_fail(current, true, false);
    current.coordinates.inside_client = false;
    current.leave_sequence = current.event_sequence;
    return HostPointerCursorOutsidePublicationResult::PublishedOutside;
}

HostPointerCaptureCleanupDecision select_host_pointer_capture_cleanup(
    bool cancel_mode,
    bool button_pressed) noexcept {
    return {
        button_pressed,
        cancel_mode || button_pressed,
    };
}

HostPointerSnapshot HostPointerSnapshotCache::snapshot() const noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool HostPointerEventRing::accept_button_transition(
    HostPointerButton button,
    bool down,
    const HostPointerCoordinates& coordinates,
    std::uint64_t absolute_acquired_ms) noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    bool* level = nullptr;
    switch (button) {
    case HostPointerButton::Left:
        level = &left_down_;
        break;
    case HostPointerButton::Right:
        level = &right_down_;
        break;
    }
    if (level == nullptr) {
        hard_fail_pointer_event_invariant();
    }
    if (*level == down) {
        return false;
    }

    *level = down;
    publish_locked(
        transition_kind(button, down),
        coordinates,
        absolute_acquired_ms);
    return true;
}

void HostPointerEventRing::invalidate_focus(
    const HostPointerCoordinates& coordinates,
    std::uint64_t absolute_acquired_ms) noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    cancel_all_locked(coordinates, absolute_acquired_ms);
}

void HostPointerEventRing::cancel_unexpected_capture(
    const HostPointerCoordinates& coordinates,
    std::uint64_t absolute_acquired_ms) noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    cancel_all_locked(coordinates, absolute_acquired_ms);
}

HostPointerTransitionPoll HostPointerEventRing::read_after(
    std::uint64_t after_sequence) const noexcept {
    const std::lock_guard<std::mutex> lock(mutex_);
    HostPointerTransitionPoll poll{};
    poll.latest_sequence = latest_sequence_;
    poll.current_focus_epoch = focus_epoch_;

    if (after_sequence >= latest_sequence_) {
        return poll;
    }

    const std::uint64_t next_sequence = after_sequence + 1u;
    const std::uint64_t retained =
        latest_sequence_ < kHostPointerTransitionCapacity
            ? latest_sequence_
            : static_cast<std::uint64_t>(kHostPointerTransitionCapacity);
    const std::uint64_t oldest_sequence = latest_sequence_ - retained + 1u;
    if (next_sequence < oldest_sequence) {
        poll.result = HostPointerTransitionReadResult::Overflow;
        return poll;
    }

    const std::size_t index = static_cast<std::size_t>(
        (next_sequence - 1u) % kHostPointerTransitionCapacity);
    poll.transition = transitions_[index];
    if (poll.transition.sequence != next_sequence) {
        hard_fail_pointer_event_invariant();
    }
    poll.result = HostPointerTransitionReadResult::Ready;
    return poll;
}

void HostPointerEventRing::cancel_all_locked(
    const HostPointerCoordinates& coordinates,
    std::uint64_t absolute_acquired_ms) noexcept {
    if (focus_epoch_ == std::numeric_limits<std::uint64_t>::max()) {
        hard_fail_pointer_event_invariant();
    }
    ++focus_epoch_;
    left_down_ = false;
    right_down_ = false;
    publish_locked(
        HostPointerTransitionKind::CancelAll,
        coordinates,
        absolute_acquired_ms);
}

void HostPointerEventRing::publish_locked(
    HostPointerTransitionKind kind,
    const HostPointerCoordinates& coordinates,
    std::uint64_t absolute_acquired_ms) noexcept {
    if (latest_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        hard_fail_pointer_event_invariant();
    }
    const std::uint64_t sequence = latest_sequence_ + 1u;
    const std::size_t index = static_cast<std::size_t>(
        (sequence - 1u) % kHostPointerTransitionCapacity);
    transitions_[index] = {
        sequence,
        focus_epoch_,
        kind,
        coordinates,
        absolute_acquired_ms,
    };
    latest_sequence_ = sequence;
}

}  // namespace galaxy::host
