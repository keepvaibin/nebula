#pragma once

#include <cstdint>
#include <optional>

namespace galaxy::timing {

// PowerPC DEC counts the raw 60.75 MHz hardware timeline. It is a separate clocked
// register, so guest time-base offsets never enter this tracker.
inline constexpr std::uint64_t kDecrementerTicksPerSecond = 60'750'000ull;
inline constexpr std::uint64_t kDecrementerWrapTicks = 1ull << 32u;

struct DecrementerDeadlineArm {
    std::uint64_t deadline_ticks{};
    std::uint64_t generation{};

    constexpr bool operator==(
        const DecrementerDeadlineArm&) const noexcept = default;
};

// Single-owner tracker for the 32-bit decrementer. The caller publishes each returned arm
// as a replaceable host deadline and passes its generation to on_deadline after consuming
// it. Requests latch here and coalesce until take_pending_request().
class DecrementerDeadlineTracker {
public:
    // Sets the counter at an exact raw tick. This is initial state, not a guest store, so it
    // cannot cause an MSB transition. A nonzero generation may be given for restored state.
    [[nodiscard]] DecrementerDeadlineArm initialize(
        std::uint64_t now_ticks,
        std::uint32_t value,
        std::uint64_t initial_generation = 1u);

    // Applies one guest DEC store at its exact raw tick. A natural transition already due, or
    // an MSB 0->1 transition caused by the store, latches a request. A prior request is kept.
    [[nodiscard]] DecrementerDeadlineArm on_write(
        std::uint64_t now_ticks,
        std::uint32_t value);

    // Observes the armed natural transition. Early or mismatched observations are invariant
    // failures. A late observation skips whole wrap cycles from the prior exact deadline.
    [[nodiscard]] DecrementerDeadlineArm on_deadline(
        std::uint64_t now_ticks,
        std::uint64_t observed_generation);

    [[nodiscard]] std::uint32_t current_value(
        std::uint64_t now_ticks) const;
    [[nodiscard]] bool pending_request() const noexcept {
        return pending_request_;
    }
    [[nodiscard]] bool take_pending_request() noexcept;
    [[nodiscard]] bool initialized() const noexcept {
        return initialized_;
    }
    [[nodiscard]] std::optional<DecrementerDeadlineArm> arm() const noexcept;

private:
    [[nodiscard]] std::uint32_t current_value_unchecked(
        std::uint64_t now_ticks) const noexcept;

    bool initialized_{};
    bool pending_request_{};
    std::uint64_t start_ticks_{};
    std::uint64_t last_event_ticks_{};
    std::uint32_t start_value_{};
    DecrementerDeadlineArm arm_{};
};

}  // namespace galaxy::timing
