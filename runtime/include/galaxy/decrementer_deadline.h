#pragma once

#include <cstdint>
#include <optional>

namespace galaxy::timing {

// PowerPC DEC advances in the raw 60.75 MHz hardware timeline. It is a
// separate clocked register: guest time-base offsets must never enter this
// tracker's API or arithmetic.
inline constexpr std::uint64_t kDecrementerTicksPerSecond = 60'750'000ull;
inline constexpr std::uint64_t kDecrementerWrapTicks = 1ull << 32u;

struct DecrementerDeadlineArm {
    std::uint64_t deadline_ticks{};
    std::uint64_t generation{};

    constexpr bool operator==(
        const DecrementerDeadlineArm&) const noexcept = default;
};

// Single-owner policy for the architectural 32-bit decrementer. The caller
// publishes each returned arm as a replaceable host-timeline deadline and
// passes that generation back to on_deadline after consuming it. Requests are
// level-latched here and coalesce until take_pending_request() acknowledges
// them.
class DecrementerDeadlineTracker {
public:
    // Establishes the counter value at an exact raw-timeline tick. This is
    // initial state, not a guest store, so it cannot itself create an MSB
    // transition. A custom nonzero generation is accepted for restored state
    // and deterministic overflow auditing.
    [[nodiscard]] DecrementerDeadlineArm initialize(
        std::uint64_t now_ticks,
        std::uint32_t value,
        std::uint64_t initial_generation = 1u);

    // Applies one guest DEC store at its exact raw-timeline tick. A natural
    // transition already due by this tick and an MSB 0->1 transition caused
    // directly by the store both latch a request. The prior request, if any,
    // is never cleared.
    [[nodiscard]] DecrementerDeadlineArm on_write(
        std::uint64_t now_ticks,
        std::uint32_t value);

    // Observes the currently armed natural transition. Early or mismatched
    // observations are invariant failures. A late observation skips complete
    // wrap cycles from the prior exact deadline, never from observation time.
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
