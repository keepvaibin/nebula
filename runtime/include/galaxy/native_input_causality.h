#pragma once

#include "galaxy/native_host.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace galaxy::input {

// RMGE01 KPADiSamplingCallback (0x80451198) derives this ring as:
//   channel_base = 0x8061D340 + channel * 0x1BF8
//   entry        = channel_base + 0x110 + write_index * 0x38
// after clamping the producer's local write index to [0, 0x78). The stored
// `bufIdx` cursor is then set to index + 1, so KPADRead legitimately observes
// the one-past-last value 0x78 before the next producer callback wraps it to
// zero. These constants are therefore a translated-code contract, not a
// generic Wii/KPAD layout assumption.
inline constexpr std::uint32_t kRmge01KpadBase = 0x8061D340u;
inline constexpr std::uint32_t kRmge01KpadChannelStride = 0x1BF8u;
inline constexpr std::uint32_t kRmge01KpadRingOffset = 0x110u;
inline constexpr std::uint32_t kRmge01KpadEntrySize = 0x38u;
inline constexpr std::uint32_t kRmge01KpadEntryCount = 0x78u;

constexpr bool is_rmge01_kpad_ring_destination(
    std::uint32_t channel,
    std::uint32_t destination) noexcept {
    if (channel >= 4u) {
        return false;
    }
    const std::uint32_t ring_base = kRmge01KpadBase +
        channel * kRmge01KpadChannelStride + kRmge01KpadRingOffset;
    const std::uint32_t ring_size =
        kRmge01KpadEntrySize * kRmge01KpadEntryCount;
    return destination >= ring_base && destination < ring_base + ring_size &&
           ((destination - ring_base) % kRmge01KpadEntrySize) == 0u;
}

static_assert(is_rmge01_kpad_ring_destination(0u, 0x8061D450u));
static_assert(!is_rmge01_kpad_ring_destination(0u, 0x8061D451u));

// Passive proof bookkeeping only. None of these states are consulted by the
// Wii Remote producer, IOS delivery cadence, translated WPAD/KPAD code, or
// game-visible memory.
enum class NativeInputCausalityReason : std::uint32_t {
    None,
    InvalidDeliveryPayload,
    InvalidDeliveryFingerprint,
    DeliveryEpochReordered,
    DeliverySampleReordered,
    DeliveryQueueOverflow,
    AllocationFailure,
    DecoderBytesMalformed,
    DecoderWithoutDelivery,
    DecoderReorderedDelivery,
    DecoderChronologyInvalid,
    DecoderFrameMismatch,
    DecoderDidNotCommit,
    DecoderSlotInvalid,
    KpadFrameMismatch,
    KpadCopySizeUnexpected,
    KpadDestinationUnexpected,
    KpadSnapshotMismatch,
    KpadSourceUntagged,
    KpadChannelMismatch,
    KpadMultipleCopies,
    KpadNoCopy,
    KpadAborted,
    KpadChronologyInvalid,
    KpadReadFrameMismatch,
    KpadReadNoProtectedSnapshot,
    KpadReadDuplicateReturnCheckpoint,
    KpadReadCallUnexpected,
    KpadReadExceptionRouteClassified,
    KpadReadArgumentsInvalid,
    KpadReadInterruptsEnabled,
    KpadReadQueueStateInvalid,
    KpadReadQueueEmpty,
    KpadReadSourceUntagged,
    KpadReadReturnCountUnexpected,
    KpadReadOutputUnavailable,
    KpadReadChronologyInvalid,
    GameplayPointerCallUnexpected,
    GameplayPointerStatusNull,
    GameplayPointerStatusUntagged,
    GameplayPointerStatusUnavailable,
    GameplayPointerChronologyInvalid,
};

[[nodiscard]] const char* native_input_causality_reason_name(
    NativeInputCausalityReason reason) noexcept;

struct NativeInputDecoderObservation {
    std::uint64_t wii_ticks{};
    std::uint32_t entry_lr{};
    std::uint32_t current_thread{};
    std::uint32_t current_context{};
};

struct NativeInputCausalKpadSample {
    bool exact{};
    NativeInputCausalityReason reason{NativeInputCausalityReason::KpadNoCopy};
    host::NativeVirtualInputAclDeliveryIdentity delivery{};
    std::uint64_t decode_sequence{};
    std::uint32_t decoder_address{};
    std::uint64_t decoder_observation_wii_ticks{};
    std::uint32_t decoder_entry_lr{};
    std::uint32_t decoder_current_thread{};
    std::uint32_t decoder_current_context{};
    std::uint32_t decoder_pending_delivery_depth{};
    std::uint64_t decoder_oldest_delivery_age_wii_ticks{};
    std::uint32_t channel{};
    std::uint32_t wpad_slot_address{};
    std::uint32_t wpad_copy_call_pc{};
    std::uint32_t wpad_copy_source{};
    std::uint32_t kpad_copy_destination{};
    std::uint32_t copied_size{};
    std::uint64_t kpad_copy_completion_wii_ticks{};
};

// Exact identity transferred from the newest 0x38-byte KPAD sampling-ring
// entry to output KPADStatus[0] by RMGE01's normal gameplay consumer,
// WPadHolder::updateReadDataOnly (KPADRead call 0x803AB2B0, max_samples=120).
// KPADRead drains oldest-to-newest while filling its output in reverse, so the
// newest protected ring entry ((write_index - 1) modulo 120) is output index
// zero; write_index itself is valid in the inclusive range [0, 120]. The
// max_samples=1 call at 0x803AA570 belongs to JUTException::readPad and is
// classified for diagnostics only; it can never satisfy gameplay IR proof.
struct NativeInputCausalKpadReadSample {
    bool exact{};
    bool gameplay_read_data_route{};
    bool observation_interrupts_disabled{};
    bool output_range_available{};
    NativeInputCausalityReason reason{
        NativeInputCausalityReason::KpadReadFrameMismatch};
    NativeInputCausalKpadSample ring_sample{};
    std::uint32_t call_pc{};
    // Populated only for a rejected return with an outstanding incompatible
    // protected frame. It is diagnostics, never a proof identity.
    std::uint32_t blocking_frame_call_pc{};
    std::uint32_t blocking_frame_depth{};
    std::uint32_t channel{};
    std::uint32_t observed_channel_base{};
    std::uint32_t output_destination{};
    std::uint32_t maximum_samples{};
    std::uint32_t source_ring_entry{};
    std::uint32_t observed_write_index{};
    std::uint32_t observed_queue_count{};
    std::uint32_t returned_samples{};
    std::uint64_t read_observation_wii_ticks{};
    std::uint64_t read_completion_wii_ticks{};
};

// Exact gameplay handoff observed when WPadPointer::update asks WPad for
// KPADStatus index zero (call 0x803ABDF0). The returned pointer must name the
// output[0] identity transferred by the preceding normal WPadHolder KPADRead.
struct NativeInputCausalGameplayPointerSample {
    bool exact{};
    NativeInputCausalityReason reason{
        NativeInputCausalityReason::GameplayPointerStatusUntagged};
    NativeInputCausalKpadReadSample kpad_read{};
    std::uint32_t call_pc{};
    std::uint32_t returned_status_pointer{};
    std::uint64_t observation_wii_ticks{};
};

// The three volatile PPC argument registers as observed immediately before a
// proven WPADRead memmove call.  The tracker retains them only until the
// ordinary post-call checkpoint verifies the actual guest-memory copy.
struct NativeInputWpadReadCopyArguments {
    std::uint32_t call_pc{};
    std::uint32_t destination{};
    std::uint32_t source{};
    std::uint32_t size{};
};

class NativeInputCausalityTracker final {
public:
    using Token = std::uint64_t;

    // Called at the exact virtual ACL /dev/usb/oh1 bulk-in delivery boundary.
    // This method is noexcept because it is installed in GuestAddressSpace's
    // non-throwing passive delivery callback.
    void observe_acl_delivery(
        const host::NativeVirtualInputAclDeliveryIdentity& identity) noexcept;

    // `decoder_bytes` starts at the report id consumed by the translated
    // report decoder (the delivery identity starts one byte earlier at A1).
    [[nodiscard]] Token begin_report_decoder(
        std::uint32_t decoder_address,
        std::uint32_t channel,
        std::span<const std::uint8_t> decoder_bytes,
        const NativeInputDecoderObservation& observation) noexcept;
    void finish_report_decoder(
        Token token,
        std::uint32_t committed_wpad_slot_address,
        bool committed) noexcept;
    void abort_report_decoder(Token token) noexcept;

    // Every RMGE01 WPAD report decoder selected by the 0x805EC4E8 dispatch
    // table is observed at entry. Invalidate both alternating status slots
    // before it can write: an exact IR decoder re-tags only its proven commit,
    // while non-IR, unmatched, aborted, and nested writers leave no stale tag.
    void invalidate_wpad_control_slots(
        std::uint32_t wpad_control_block) noexcept;
    void observe_transport_generation(
        std::uint64_t epochs_started,
        std::uint64_t mode_invalidations,
        std::uint64_t disconnect_resets,
        bool armed) noexcept;

    [[nodiscard]] Token begin_kpad_sample(std::uint32_t channel) noexcept;

    // The pre-call observation retains the architectural arguments before the
    // translated memmove helper may clobber its volatile registers. Completion
    // is observed at the existing call-return checkpoint, where
    // `snapshot_equal` is measured from the actual guest bytes; it is never
    // inferred. The combined method is retained for unit-level lifecycle tests.
    void begin_wpad_read_copy(
        std::uint32_t call_pc,
        std::uint32_t destination,
        std::uint32_t source,
        std::uint32_t size) noexcept;
    [[nodiscard]] std::optional<NativeInputWpadReadCopyArguments>
    pending_wpad_read_copy(std::uint32_t call_pc) const noexcept;
    void finish_wpad_read_copy(
        std::uint32_t call_pc,
        bool snapshot_equal,
        std::uint64_t copy_completion_wii_ticks) noexcept;
    void observe_wpad_read_copy(
        std::uint32_t call_pc,
        std::uint32_t destination,
        std::uint32_t source,
        std::uint32_t size,
        bool snapshot_equal,
        std::uint64_t copy_completion_wii_ticks) noexcept;

    [[nodiscard]] NativeInputCausalKpadSample finish_kpad_sample(
        Token token) noexcept;
    void abort_kpad_sample(Token token) noexcept;

    // 0x8045080C is the OSDisableInterrupts call-return checkpoint. At its
    // continuation (0x80450810), interrupts are disabled and KPADRead has not
    // yet loaded or cleared the queue, so write_index and queue_count form one
    // protected snapshot. 0x803AB2B0 is the normal gameplay outer call site.
    void begin_classified_kpad_read(
        std::uint32_t call_pc,
        std::uint32_t channel,
        std::uint32_t channel_base,
        std::uint32_t output_destination,
        std::uint32_t maximum_samples,
        std::uint32_t write_index,
        std::uint32_t queue_count,
        bool observation_interrupts_disabled,
        bool output_range_available,
        std::uint64_t observation_wii_ticks) noexcept;
    [[nodiscard]] NativeInputCausalKpadReadSample finish_classified_kpad_read(
        std::uint32_t call_pc,
        std::uint32_t returned_samples,
        std::uint32_t caller_channel,
        std::uint32_t caller_output_destination,
        bool caller_output_available,
        std::uint64_t completion_wii_ticks) noexcept;

    [[nodiscard]] NativeInputCausalGameplayPointerSample
    observe_gameplay_pointer_status(
        std::uint32_t call_pc,
        std::uint32_t returned_status_pointer,
        bool status_range_available,
        std::uint64_t observation_wii_ticks) noexcept;

    [[nodiscard]] NativeInputCausalityReason protocol_failure() const noexcept;

private:
    struct PendingDelivery {
        host::NativeVirtualInputAclDeliveryIdentity identity{};
    };
    struct DecoderFrame {
        Token token{};
        std::uint64_t decode_sequence{};
        std::uint32_t decoder_address{};
        std::uint32_t channel{};
        NativeInputDecoderObservation observation{};
        std::uint32_t pending_delivery_depth{};
        std::uint64_t oldest_delivery_age_wii_ticks{};
        host::NativeVirtualInputAclDeliveryIdentity delivery{};
    };
    struct WpadSlotIdentity {
        std::uint64_t decode_sequence{};
        std::uint32_t decoder_address{};
        std::uint32_t channel{};
        NativeInputDecoderObservation observation{};
        std::uint32_t pending_delivery_depth{};
        std::uint64_t oldest_delivery_age_wii_ticks{};
        host::NativeVirtualInputAclDeliveryIdentity delivery{};
    };
    struct KpadFrame {
        Token token{};
        std::uint32_t channel{};
        std::uint32_t copy_count{};
        bool have_ring_destination{};
        std::uint32_t ring_destination{};
        std::optional<NativeInputWpadReadCopyArguments> pending_copy{};
        NativeInputCausalKpadSample sample{};
    };
    struct KpadReadFrame {
        Token token{};
        NativeInputCausalKpadReadSample sample{};
    };

    void set_protocol_failure_locked(
        NativeInputCausalityReason reason) noexcept;
    [[nodiscard]] static bool is_supported_ir_delivery(
        const host::NativeVirtualInputAclDeliveryIdentity& identity) noexcept;
    [[nodiscard]] static bool decoder_matches_delivery(
        std::uint32_t decoder_address,
        std::span<const std::uint8_t> decoder_bytes,
        const host::NativeVirtualInputAclDeliveryIdentity& identity) noexcept;
    [[nodiscard]] static std::uint32_t expected_copy_size(
        std::uint32_t call_pc) noexcept;
    static constexpr std::size_t kMaximumPendingDeliveries = 4096u;

    mutable std::mutex mutex_;
    std::deque<PendingDelivery> pending_deliveries_;
    // At a report-mode/epoch boundary, a translated decoder can still revisit
    // bytes delivered in the immediately preceding generation while the first
    // new-generation packet is already queued.  Retain only that displaced
    // FIFO so an exact old-byte revisit is explicitly classified as
    // untracked; it can never establish a new identity.
    std::deque<PendingDelivery> retired_deliveries_;
    std::vector<DecoderFrame> decoder_frames_;
    std::vector<KpadFrame> kpad_frames_;
    std::vector<KpadReadFrame> kpad_read_frames_;
    std::unordered_map<std::uint32_t, WpadSlotIdentity> wpad_slot_identities_;
    std::unordered_map<std::uint32_t, NativeInputCausalKpadSample>
        kpad_ring_identities_;
    std::unordered_map<std::uint32_t, NativeInputCausalKpadReadSample>
        gameplay_output_identities_;
    NativeInputCausalityReason protocol_failure_{
        NativeInputCausalityReason::None};
    bool have_last_delivery_{};
    std::uint64_t last_delivery_epoch_{};
    std::uint64_t last_delivery_sample_{};
    std::uint8_t last_delivery_report_id_{};
    bool have_transport_generation_{};
    std::uint64_t transport_epochs_started_{};
    std::uint64_t transport_mode_invalidations_{};
    std::uint64_t transport_disconnect_resets_{};
    bool transport_armed_{};
    // This is diagnostic provenance only: it records that native ACL traffic
    // has crossed the IOS boundary, but a later decoder with no queued report
    // remains an untracked guest-state revisit rather than a fabricated
    // delivery. Exact proof still requires a queued delivery and byte match.
    bool have_observed_native_delivery_{};
    Token next_token_{1u};
    std::uint64_t next_decode_sequence_{1u};
};

}  // namespace galaxy::input
