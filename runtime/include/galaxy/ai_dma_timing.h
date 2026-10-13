#pragma once

#include "galaxy/runtime_timeline.h"

#include <cstddef>
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

// A verified host suspension removes its interval from guest time. The AI
// device sits on that timeline, so the buffers those periods would have
// produced are never made: the output queue loses that much depth for good
// and the next hitch empties it (an audible gap plus a full re-prebuffer).
// The removed interval is owed back by pulling the next AI completion earlier;
// the ordinary late-service path then produces those buffers as a burst.
// Capped so a long suspension (sleep/resume) adds at most this many buffers of
// latency; cap plus one period of ordinary lateness stays under the masked
// catch-up bound of 8 periods in service_ai_dma.
inline constexpr std::uint64_t kHostPauseAiCatchUpPeriods = 6u;

[[nodiscard]] constexpr std::uint64_t host_pause_ai_catch_up_ticks(
    std::uint64_t owed_ticks,
    std::uint64_t next_completion_ticks,
    std::uint64_t duration_ticks) noexcept {
    if (owed_ticks == 0u || next_completion_ticks <= 1u ||
        duration_ticks == 0u ||
        duration_ticks > ~std::uint64_t{0} / kHostPauseAiCatchUpPeriods) {
        return 0u;
    }
    const std::uint64_t cap = duration_ticks * kHostPauseAiCatchUpPeriods;
    const std::uint64_t shift = owed_ticks < cap ? owed_ticks : cap;
    return shift < next_completion_ticks ? shift : next_completion_ticks - 1u;
}

// JASDriver::updateDac (0x80494390) runs on the game's audio thread once per
// AI callback: AIInitDMA of the buffer it mixed last (AID_LEN is the last
// register written), then readDspBuffer of the next frame from the DSP output
// ring into the following buffer. A completion that falls due before that
// thread ran for the previous callback finds the shadow still naming the
// buffer just played, and the hardware would play it again: one 17.5 ms block
// repeated. A callback that runs before the DSP ring holds a frame makes the
// game fill the buffer with its last sample (readDspBuffer underrun 0x804945CC:
// a flat block and two steps). Both happen whenever completions are late (a
// host stall; every guest thread runs on the simulation thread, so late
// completions arrive back to back while the ring holds only the frames mixed
// ahead). Such a completion is held until the game wrote AID_LEN and the ring
// holds a frame, on the device's original period grid, so the late periods are
// produced once each at the pace the game mixes them and the output queue
// regains its depth. Without a write for kStaleAiHoldPeriods (the game stopped
// feeding the DMA) the boundary is latched as the hardware would, but played as
// a fade to silence instead of a repeat. The limit outlasts host stalls in
// which no guest thread can run (a frame boundary waiting for the GPU inside an
// interrupt handler measured 50-78 ms); a shorter one turned each such stall
// into inserted silence while the output queue still had audio. It equals the
// masked catch-up bound of service_ai_dma, beyond which periods are dropped.
inline constexpr std::uint64_t kStaleAiHoldPeriods = 8u;
// A hold that waits for the DSP ring re-checks it this often (in periods).
inline constexpr std::uint64_t kAiSourceRecheckDivisor = 8u;

enum class StaleAiCompletion : std::uint8_t {
    Latch,           // the shadow names a new buffer
    Hold,            // wait for the game's AID_LEN write or its next DSP frame
    LatchConcealed,  // hold timed out: latch, output a fade to silence
};

[[nodiscard]] constexpr std::uint64_t stale_ai_hold_release_ticks(
    std::uint64_t hold_since_ticks,
    std::uint64_t duration_ticks) noexcept {
    const std::uint64_t span = duration_ticks <= ~std::uint64_t{0} / kStaleAiHoldPeriods
        ? duration_ticks * kStaleAiHoldPeriods
        : ~std::uint64_t{0};
    return hold_since_ticks <= ~std::uint64_t{0} - span
        ? hold_since_ticks + span
        : ~std::uint64_t{0};
}

// `hold_since_ticks` is 0 when no hold is running (a hold then starts now).
// `source_ready`: the game's DSP ring holds a frame for the next callback.
// `feed_stalled`: a hold already timed out and the game has not written since;
// later boundaries keep the period grid and are concealed without a hold.
[[nodiscard]] constexpr StaleAiCompletion stale_ai_completion_action(
    bool shadow_fresh,
    bool source_ready,
    bool feed_stalled,
    std::uint64_t hold_since_ticks,
    std::uint64_t now_ticks,
    std::uint64_t duration_ticks) noexcept {
    if (shadow_fresh && source_ready) {
        return StaleAiCompletion::Latch;
    }
    if (feed_stalled && !shadow_fresh) {
        return StaleAiCompletion::LatchConcealed;
    }
    if (duration_ticks == 0u) {
        return StaleAiCompletion::Latch;
    }
    if (hold_since_ticks == 0u ||
        now_ticks < stale_ai_hold_release_ticks(hold_since_ticks, duration_ticks)) {
        return StaleAiCompletion::Hold;
    }
    // Timed out: a written buffer plays as written; a replay is concealed.
    return shadow_fresh ? StaleAiCompletion::Latch
                        : StaleAiCompletion::LatchConcealed;
}

// Concealment ramps for big-endian 16-bit stereo AI PCM: 4 ms at 32 kHz, short
// enough to hide a step without an audible fade.
inline constexpr std::size_t kConcealRampFrames = 128u;

[[nodiscard]] constexpr std::int16_t load_be16(const std::byte* p) noexcept {
    return static_cast<std::int16_t>(
        (std::to_integer<std::uint16_t>(p[0]) << 8) | std::to_integer<std::uint16_t>(p[1]));
}

constexpr void store_be16(std::byte* p, std::int32_t value) noexcept {
    const auto bits = static_cast<std::uint16_t>(static_cast<std::int16_t>(value));
    p[0] = static_cast<std::byte>(bits >> 8);
    p[1] = static_cast<std::byte>(bits & 0xFFu);
}

// Fills `out` (whole frames) with a ramp from the last output sample pair to
// silence, then silence.
constexpr void write_conceal_fade_out(
    std::byte* out, std::size_t bytes, std::int16_t left, std::int16_t right) noexcept {
    const std::size_t frames = bytes / 4u;
    for (std::size_t i = 0; i < frames; ++i) {
        const std::int32_t remaining = i < kConcealRampFrames
            ? static_cast<std::int32_t>(kConcealRampFrames - 1u - i)
            : 0;
        constexpr auto kSteps = static_cast<std::int32_t>(kConcealRampFrames);
        store_be16(out + i * 4u, left * remaining / kSteps);
        store_be16(out + i * 4u + 2u, right * remaining / kSteps);
    }
}

// Ramps the first kConcealRampFrames frames of `pcm` up from silence in place.
constexpr void apply_conceal_fade_in(std::byte* pcm, std::size_t bytes) noexcept {
    const std::size_t frames = bytes / 4u;
    const std::size_t ramp = frames < kConcealRampFrames ? frames : kConcealRampFrames;
    constexpr auto kSteps = static_cast<std::int32_t>(kConcealRampFrames);
    for (std::size_t i = 0; i < ramp; ++i) {
        const auto gain = static_cast<std::int32_t>(i);
        for (std::size_t c = 0; c < 2u; ++c) {
            std::byte* sample = pcm + i * 4u + c * 2u;
            store_be16(sample, load_be16(sample) * gain / kSteps);
        }
    }
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
