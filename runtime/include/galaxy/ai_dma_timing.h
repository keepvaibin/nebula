#pragma once

#include "galaxy/runtime_timeline.h"

#include <cstdint>
#include <utility>

namespace galaxy::host::ai_dma_timing {

// The model treats the start AID edge and register-latch ordering separately
// from this delay value. No physical-Wii timing capture or authoritative
// hardware evidence currently establishes an exact delay. The same numeric
// value appears in emulator source as a provisional CPU-cycle workaround; this
// runtime instead measures it in 60.75 MHz time-base ticks, so the number is
// not unit-equivalent and must never be called hardware-exact. Preserve current
// behavior until a hardware capture supplies a replacement.
inline constexpr std::uint64_t kInitialInterruptDelayTimelineTicks = 200u;
inline constexpr bool kInitialInterruptDelayHardwareValidated = false;
inline constexpr char kInitialInterruptDelayAuditStatus[] =
    "provisional-unverified";

static_assert(timing::kTimelineTicksPerSecond == 60'750'000ull);

// Clear selected bits for one native device transaction and restore the exact
// original word on every C++ exit path. A null word represents a boundary that
// has no live guest context and is therefore already non-interruptible.
class ScopedExactWordBitClear final {
public:
    ScopedExactWordBitClear(
        std::uint32_t* word,
        std::uint32_t clear_bits) noexcept
        : word_(word), saved_(word != nullptr ? *word : 0u) {
        if (word_ != nullptr) {
            *word_ &= ~clear_bits;
        }
    }

    ~ScopedExactWordBitClear() noexcept {
        if (word_ != nullptr) {
            *word_ = saved_;
        }
    }

    ScopedExactWordBitClear(const ScopedExactWordBitClear&) = delete;
    ScopedExactWordBitClear& operator=(const ScopedExactWordBitClear&) = delete;
    ScopedExactWordBitClear(ScopedExactWordBitClear&&) = delete;
    ScopedExactWordBitClear& operator=(ScopedExactWordBitClear&&) = delete;

private:
    std::uint32_t* word_{};
    std::uint32_t saved_{};
};

// A translated dynamic-call boundary has no caller PC in the host ABI, so it
// cannot safely enter a guest interrupt handler there. It may still probe the
// native AI device with EE temporarily masked. That work must precede virtual
// HID collection: a host input poll can cross the AI deadline, and servicing a
// second queued input edge first can turn two individually bounded host delays
// into a full-buffer audio overrun.
[[nodiscard]] constexpr bool ai_probe_precedes_native_input(
    bool ai_device_active,
    bool servicing_native_input,
    bool ai_transaction_owned) noexcept {
    return ai_device_active && !servicing_native_input &&
        !ai_transaction_owned;
}

// IRQ5 ownership starts at the translated dispatcher's exact indirect handler
// call, before RuntimeState's scoped handler flag is set, and lasts through the
// handler's W1C/shadow-register transaction. Model both the durable dispatcher
// record and that selection-call edge so no input poll can slip between them.
[[nodiscard]] constexpr bool ai_device_transaction_owned(
    bool scoped_handler_active,
    bool external_dispatch_active,
    std::uint32_t selected_interrupt,
    bool selected_handler_returned,
    bool exact_ai_handler_selection_call) noexcept {
    constexpr std::uint32_t kAiInterrupt = 5u;
    return scoped_handler_active || exact_ai_handler_selection_call ||
        (external_dispatch_active && selected_interrupt == kAiInterrupt &&
         !selected_handler_returned);
}

// While an AI callback owns its shadow-register transaction, no native input
// collection may begin, even if the next deadline has not been published yet.
// Otherwise publication can race a blocking poll and place that full delay in
// front of the owning callback.
[[nodiscard]] constexpr bool native_input_must_wait_for_ai_owner(
    bool servicing_native_input,
    bool ai_transaction_owned) noexcept {
    return !servicing_native_input && ai_transaction_owned;
}

enum class NativeInputAiServiceResult : std::uint8_t {
    RecursiveInput,
    DeferredForAiOwner,
    NoInputPending,
    InputServiceAttempted,
};

// Keep the production ordering in one deterministic policy surface: an AI
// probe precedes a potentially blocking input acquisition, owner state is
// rechecked after that probe, and a second AI probe follows the input work.
template <
    typename AiTransactionOwned,
    typename ProbeAi,
    typename InputPending,
    typename ServiceInput>
[[nodiscard]] NativeInputAiServiceResult service_native_input_with_ai_priority(
    bool servicing_native_input,
    AiTransactionOwned&& ai_transaction_owned,
    ProbeAi&& probe_ai,
    InputPending&& input_pending,
    ServiceInput&& service_input) {
    if (servicing_native_input) {
        return NativeInputAiServiceResult::RecursiveInput;
    }
    if (std::forward<AiTransactionOwned>(ai_transaction_owned)()) {
        return NativeInputAiServiceResult::DeferredForAiOwner;
    }
    std::forward<ProbeAi>(probe_ai)();
    if (std::forward<AiTransactionOwned>(ai_transaction_owned)()) {
        return NativeInputAiServiceResult::DeferredForAiOwner;
    }
    if (!std::forward<InputPending>(input_pending)()) {
        return NativeInputAiServiceResult::NoInputPending;
    }
    std::forward<ServiceInput>(service_input)();
    std::forward<ProbeAi>(probe_ai)();
    return NativeInputAiServiceResult::InputServiceAttempted;
}

// A host-suspension rebase may restore AI only when the device, RuntimeState,
// and broker publication all describe the same still-owned arm. A publication
// can be absent from the latch solely when the simulation thread has already
// taken that exact generation and carries it in locally_taken_generation.
[[nodiscard]] constexpr bool exact_host_suspension_arm_identity(
    bool playing,
    bool interrupt_pending,
    bool armed,
    std::uint64_t armed_deadline_ticks,
    std::uint64_t armed_generation,
    std::uint64_t pending_generation,
    std::uint64_t locally_taken_generation,
    std::uint64_t device_completion_ticks,
    std::uint64_t device_duration_ticks) noexcept {
    const bool pending_identity_valid =
        pending_generation == 0u || pending_generation == armed_generation;
    const bool local_identity_valid = locally_taken_generation == 0u ||
        locally_taken_generation == armed_generation;
    const bool exact_publication_owned =
        pending_generation == armed_generation ||
        locally_taken_generation == armed_generation;
    return playing && !interrupt_pending && armed &&
        armed_deadline_ticks != 0u && armed_generation != 0u &&
        device_completion_ticks == armed_deadline_ticks &&
        device_duration_ticks != 0u && pending_identity_valid &&
        local_identity_valid && exact_publication_owned;
}

}  // namespace galaxy::host::ai_dma_timing
