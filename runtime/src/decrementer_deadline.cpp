#include "galaxy/decrementer_deadline.h"

#include <limits>
#include <stdexcept>

namespace galaxy::timing {
namespace {

constexpr bool msb_is_set(std::uint32_t value) noexcept {
    return (value & 0x8000'0000u) != 0u;
}

std::uint64_t checked_first_deadline(
    std::uint64_t start_ticks,
    std::uint32_t value) {
    const std::uint64_t ticks_until_transition =
        static_cast<std::uint64_t>(value) + 1u;
    if (start_ticks >
        std::numeric_limits<std::uint64_t>::max() -
            ticks_until_transition) {
        throw std::overflow_error(
            "decrementer first-transition deadline overflow");
    }
    return start_ticks + ticks_until_transition;
}

std::uint64_t checked_next_generation(std::uint64_t generation) {
    if (generation == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("decrementer generation overflow");
    }
    return generation + 1u;
}

std::uint64_t checked_next_wrap_deadline(
    std::uint64_t deadline_ticks,
    std::uint64_t now_ticks) {
    const std::uint64_t elapsed = now_ticks - deadline_ticks;
    const std::uint64_t elapsed_wraps = elapsed / kDecrementerWrapTicks;
    const std::uint64_t wraps_to_next = elapsed_wraps + 1u;
    const std::uint64_t available =
        std::numeric_limits<std::uint64_t>::max() - deadline_ticks;
    if (wraps_to_next > available / kDecrementerWrapTicks) {
        throw std::overflow_error(
            "decrementer recurring deadline overflow");
    }
    return deadline_ticks + wraps_to_next * kDecrementerWrapTicks;
}

}  // namespace

DecrementerDeadlineArm DecrementerDeadlineTracker::initialize(
    std::uint64_t now_ticks,
    std::uint32_t value,
    std::uint64_t initial_generation) {
    if (initialized_) {
        throw std::logic_error(
            "decrementer deadline tracker is already initialized");
    }
    if (initial_generation == 0u) {
        throw std::invalid_argument(
            "decrementer generation must be nonzero");
    }

    const DecrementerDeadlineArm next_arm{
        checked_first_deadline(now_ticks, value), initial_generation};
    start_ticks_ = now_ticks;
    last_event_ticks_ = now_ticks;
    start_value_ = value;
    arm_ = next_arm;
    pending_request_ = false;
    initialized_ = true;
    return arm_;
}

DecrementerDeadlineArm DecrementerDeadlineTracker::on_write(
    std::uint64_t now_ticks,
    std::uint32_t value) {
    if (!initialized_) {
        throw std::logic_error(
            "decrementer write observed before initialization");
    }
    if (now_ticks < last_event_ticks_) {
        throw std::invalid_argument(
            "decrementer timeline moved backwards on write");
    }

    // Compute every fallible replacement property before changing state. A
    // failed audit leaves the previous counter and request latch intact.
    const DecrementerDeadlineArm next_arm{
        checked_first_deadline(now_ticks, value),
        checked_next_generation(arm_.generation)};
    const std::uint32_t old_value = current_value_unchecked(now_ticks);
    const bool elapsed_transition = now_ticks >= arm_.deadline_ticks;
    const bool store_transition =
        !msb_is_set(old_value) && msb_is_set(value);

    pending_request_ =
        pending_request_ || elapsed_transition || store_transition;
    start_ticks_ = now_ticks;
    last_event_ticks_ = now_ticks;
    start_value_ = value;
    arm_ = next_arm;
    return arm_;
}

DecrementerDeadlineArm DecrementerDeadlineTracker::on_deadline(
    std::uint64_t now_ticks,
    std::uint64_t observed_generation) {
    if (!initialized_) {
        throw std::logic_error(
            "decrementer deadline observed before initialization");
    }
    if (observed_generation != arm_.generation) {
        throw std::invalid_argument(
            "decrementer deadline generation mismatch");
    }
    if (now_ticks < arm_.deadline_ticks) {
        throw std::runtime_error(
            "decrementer deadline observed before it was due");
    }

    // Rearm from the immutable prior edge so late observations cannot move
    // the hardware phase. Calculate before latching to preserve a fail-closed
    // state if the process timeline can no longer represent another edge.
    const DecrementerDeadlineArm next_arm{
        checked_next_wrap_deadline(arm_.deadline_ticks, now_ticks),
        checked_next_generation(arm_.generation)};
    pending_request_ = true;
    last_event_ticks_ = now_ticks;
    arm_ = next_arm;
    return arm_;
}

std::uint32_t DecrementerDeadlineTracker::current_value(
    std::uint64_t now_ticks) const {
    if (!initialized_) {
        throw std::logic_error(
            "decrementer value queried before initialization");
    }
    if (now_ticks < last_event_ticks_) {
        throw std::invalid_argument(
            "decrementer timeline moved backwards on read");
    }
    return current_value_unchecked(now_ticks);
}

bool DecrementerDeadlineTracker::take_pending_request() noexcept {
    const bool was_pending = pending_request_;
    pending_request_ = false;
    return was_pending;
}

std::optional<DecrementerDeadlineArm>
DecrementerDeadlineTracker::arm() const noexcept {
    if (!initialized_) {
        return std::nullopt;
    }
    return arm_;
}

std::uint32_t DecrementerDeadlineTracker::current_value_unchecked(
    std::uint64_t now_ticks) const noexcept {
    const auto elapsed = static_cast<std::uint32_t>(now_ticks - start_ticks_);
    return start_value_ - elapsed;
}

}  // namespace galaxy::timing
