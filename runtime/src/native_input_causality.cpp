#include "galaxy/native_input_causality.h"

#include <algorithm>
#include <array>
#include <limits>

namespace galaxy::input {
namespace {

constexpr std::uint64_t kFnv1a64OffsetBasis =
    14'695'981'039'346'656'037ull;
constexpr std::uint64_t kFnv1a64Prime = 1'099'511'628'211ull;

std::uint64_t payload_fingerprint(
    const host::NativeVirtualInputAclDeliveryIdentity& identity) noexcept {
    std::uint64_t result = kFnv1a64OffsetBasis;
    for (std::uint32_t index = 0; index < identity.payload_size; ++index) {
        result ^= static_cast<std::uint64_t>(identity.payload[index]);
        result *= kFnv1a64Prime;
    }
    return result;
}

std::uint8_t decoder_report_id(std::uint32_t decoder_address) noexcept {
    switch (decoder_address) {
    case 0x804DFF98u:
        return 0x33u;
    case 0x804E0C98u:
        return 0x36u;
    case 0x804E0ED0u:
        return 0x37u;
    default:
        return 0u;
    }
}

std::uint32_t decoder_byte_count(std::uint8_t report_id) noexcept {
    switch (report_id) {
    case 0x33u:
        return 18u;
    case 0x36u:
    case 0x37u:
        return 22u;
    default:
        return 0u;
    }
}

}  // namespace

const char* native_input_causality_reason_name(
    NativeInputCausalityReason reason) noexcept {
    switch (reason) {
    case NativeInputCausalityReason::None:
        return "none";
    case NativeInputCausalityReason::InvalidDeliveryPayload:
        return "invalid-delivery-payload";
    case NativeInputCausalityReason::InvalidDeliveryFingerprint:
        return "invalid-delivery-fingerprint";
    case NativeInputCausalityReason::DeliveryEpochReordered:
        return "delivery-epoch-reordered";
    case NativeInputCausalityReason::DeliverySampleReordered:
        return "delivery-sample-reordered";
    case NativeInputCausalityReason::DeliveryQueueOverflow:
        return "delivery-queue-overflow";
    case NativeInputCausalityReason::AllocationFailure:
        return "allocation-failure";
    case NativeInputCausalityReason::DecoderBytesMalformed:
        return "decoder-bytes-malformed";
    case NativeInputCausalityReason::DecoderWithoutDelivery:
        return "decoder-without-delivery";
    case NativeInputCausalityReason::DecoderReorderedDelivery:
        return "decoder-reordered-delivery";
    case NativeInputCausalityReason::DecoderChronologyInvalid:
        return "decoder-chronology-invalid";
    case NativeInputCausalityReason::DecoderFrameMismatch:
        return "decoder-frame-mismatch";
    case NativeInputCausalityReason::DecoderDidNotCommit:
        return "decoder-did-not-commit";
    case NativeInputCausalityReason::DecoderSlotInvalid:
        return "decoder-slot-invalid";
    case NativeInputCausalityReason::KpadFrameMismatch:
        return "kpad-frame-mismatch";
    case NativeInputCausalityReason::KpadCopySizeUnexpected:
        return "kpad-copy-size-unexpected";
    case NativeInputCausalityReason::KpadDestinationUnexpected:
        return "kpad-destination-unexpected";
    case NativeInputCausalityReason::KpadSnapshotMismatch:
        return "kpad-snapshot-mismatch";
    case NativeInputCausalityReason::KpadSourceUntagged:
        return "kpad-source-untagged";
    case NativeInputCausalityReason::KpadChannelMismatch:
        return "kpad-channel-mismatch";
    case NativeInputCausalityReason::KpadMultipleCopies:
        return "kpad-multiple-copies";
    case NativeInputCausalityReason::KpadNoCopy:
        return "kpad-no-copy";
    case NativeInputCausalityReason::KpadAborted:
        return "kpad-aborted";
    case NativeInputCausalityReason::KpadChronologyInvalid:
        return "kpad-chronology-invalid";
    case NativeInputCausalityReason::KpadReadFrameMismatch:
        return "kpad-read-frame-mismatch";
    case NativeInputCausalityReason::KpadReadNoProtectedSnapshot:
        return "kpad-read-no-protected-snapshot";
    case NativeInputCausalityReason::KpadReadDuplicateReturnCheckpoint:
        return "kpad-read-duplicate-return-checkpoint";
    case NativeInputCausalityReason::KpadReadCallUnexpected:
        return "kpad-read-call-unexpected";
    case NativeInputCausalityReason::KpadReadExceptionRouteClassified:
        return "kpad-read-exception-route-classified";
    case NativeInputCausalityReason::KpadReadArgumentsInvalid:
        return "kpad-read-arguments-invalid";
    case NativeInputCausalityReason::KpadReadInterruptsEnabled:
        return "kpad-read-interrupts-enabled";
    case NativeInputCausalityReason::KpadReadQueueStateInvalid:
        return "kpad-read-queue-state-invalid";
    case NativeInputCausalityReason::KpadReadQueueEmpty:
        return "kpad-read-queue-empty";
    case NativeInputCausalityReason::KpadReadSourceUntagged:
        return "kpad-read-source-untagged";
    case NativeInputCausalityReason::KpadReadReturnCountUnexpected:
        return "kpad-read-return-count-unexpected";
    case NativeInputCausalityReason::KpadReadOutputUnavailable:
        return "kpad-read-output-unavailable";
    case NativeInputCausalityReason::KpadReadChronologyInvalid:
        return "kpad-read-chronology-invalid";
    case NativeInputCausalityReason::GameplayPointerCallUnexpected:
        return "gameplay-pointer-call-unexpected";
    case NativeInputCausalityReason::GameplayPointerStatusNull:
        return "gameplay-pointer-status-null";
    case NativeInputCausalityReason::GameplayPointerStatusUntagged:
        return "gameplay-pointer-status-untagged";
    case NativeInputCausalityReason::GameplayPointerStatusUnavailable:
        return "gameplay-pointer-status-unavailable";
    case NativeInputCausalityReason::GameplayPointerChronologyInvalid:
        return "gameplay-pointer-chronology-invalid";
    }
    return "unknown";
}

void NativeInputCausalityTracker::set_protocol_failure_locked(
    NativeInputCausalityReason reason) noexcept {
    if (protocol_failure_ == NativeInputCausalityReason::None) {
        protocol_failure_ = reason;
    }
}

bool NativeInputCausalityTracker::is_supported_ir_delivery(
    const host::NativeVirtualInputAclDeliveryIdentity& identity) noexcept {
    return identity.report_id == 0x33u || identity.report_id == 0x36u ||
           identity.report_id == 0x37u;
}

void NativeInputCausalityTracker::observe_acl_delivery(
    const host::NativeVirtualInputAclDeliveryIdentity& identity) noexcept {
    std::lock_guard lock(mutex_);
    if (protocol_failure_ != NativeInputCausalityReason::None) {
        return;
    }
    if (!is_supported_ir_delivery(identity)) {
        return;
    }

    const std::uint32_t expected_payload_size =
        identity.report_id == 0x33u ? 19u : 23u;
    if (identity.payload_size != expected_payload_size ||
        identity.payload_size > identity.payload.size() ||
        identity.payload[0] != 0xA1u ||
        identity.payload[1] != identity.report_id) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::InvalidDeliveryPayload);
        return;
    }
    if (payload_fingerprint(identity) != identity.payload_fingerprint) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::InvalidDeliveryFingerprint);
        return;
    }
    if (have_last_delivery_) {
        if (identity.logical_epoch < last_delivery_epoch_) {
            set_protocol_failure_locked(
                NativeInputCausalityReason::DeliveryEpochReordered);
            return;
        }
        if (identity.logical_epoch == last_delivery_epoch_ &&
            identity.sample_sequence <= last_delivery_sample_) {
            set_protocol_failure_locked(
                NativeInputCausalityReason::DeliverySampleReordered);
            return;
        }
    }
    if (pending_deliveries_.size() >= kMaximumPendingDeliveries) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::DeliveryQueueOverflow);
        return;
    }

    // A cadence epoch begins on arm/reconnect and on report-mode
    // invalidation. A report-id change is also treated as a mode boundary even
    // if a corrupted producer failed to advance its epoch. Neither boundary
    // may retain an undecoded delivery or inherit any downstream proof tag.
    if (have_last_delivery_ &&
        (identity.logical_epoch != last_delivery_epoch_ ||
         identity.report_id != last_delivery_report_id_)) {
        // Preserve the displaced FIFO only long enough to recognize an exact
        // guest-state revisit from the immediately preceding generation. A
        // swap is allocation-free inside this noexcept tracker path. Those
        // retired identities are diagnostic-only and are never promoted into
        // decoder, KPAD, or gameplay-output proof state.
        retired_deliveries_.clear();
        retired_deliveries_.swap(pending_deliveries_);
        decoder_frames_.clear();
        kpad_frames_.clear();
        kpad_read_frames_.clear();
        wpad_slot_identities_.clear();
        kpad_ring_identities_.clear();
        gameplay_output_identities_.clear();
    }

    try {
        pending_deliveries_.push_back(PendingDelivery{identity});
    } catch (...) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::AllocationFailure);
        return;
    }
    have_observed_native_delivery_ = true;
    have_last_delivery_ = true;
    last_delivery_epoch_ = identity.logical_epoch;
    last_delivery_sample_ = identity.sample_sequence;
    last_delivery_report_id_ = identity.report_id;
}

bool NativeInputCausalityTracker::decoder_matches_delivery(
    std::uint32_t decoder_address,
    std::span<const std::uint8_t> decoder_bytes,
    const host::NativeVirtualInputAclDeliveryIdentity& identity) noexcept {
    const std::uint8_t report_id = decoder_report_id(decoder_address);
    const std::uint32_t byte_count = decoder_byte_count(report_id);
    if (report_id == 0u || identity.report_id != report_id ||
        decoder_bytes.size() != byte_count ||
        identity.payload_size != byte_count + 1u) {
        return false;
    }
    for (std::size_t index = 0; index < decoder_bytes.size(); ++index) {
        if (decoder_bytes[index] != identity.payload[index + 1u]) {
            return false;
        }
    }
    return true;
}

NativeInputCausalityTracker::Token
NativeInputCausalityTracker::begin_report_decoder(
    std::uint32_t decoder_address,
    std::uint32_t channel,
    std::span<const std::uint8_t> decoder_bytes,
    const NativeInputDecoderObservation& observation) noexcept {
    std::lock_guard lock(mutex_);
    if (protocol_failure_ != NativeInputCausalityReason::None) {
        return 0u;
    }
    const std::uint8_t expected_report_id =
        decoder_report_id(decoder_address);
    const std::uint32_t expected_size =
        decoder_byte_count(expected_report_id);
    if (expected_report_id == 0u || decoder_bytes.size() != expected_size ||
        decoder_bytes.empty() || decoder_bytes.front() != expected_report_id ||
        channel >= 4u) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::DecoderBytesMalformed);
        return 0u;
    }
    if (pending_deliveries_.empty()) {
        // A report decoder can legitimately revisit its guest WPAD state
        // after the IOS bulk-in delivery that originally populated it has
        // already been consumed. This also covers RMGE01's startup and
        // report-mode teardown paths. With no queued ACL delivery there is no
        // identity to prove, so leave this call untracked. It cannot inherit
        // a stale slot identity: the runtime invalidates every translated
        // decoder's alternating WPAD slots before entering this tracker, and
        // a later exact path must still begin with a newly queued delivery.
        return 0u;
    }

    const auto matching = std::find_if(
        pending_deliveries_.begin(),
        pending_deliveries_.end(),
        [&](const PendingDelivery& pending) {
            return decoder_matches_delivery(
                decoder_address, decoder_bytes, pending.identity);
        });
    if (matching != pending_deliveries_.begin()) {
        if (matching == pending_deliveries_.end()) {
            const auto retired = std::find_if(
                retired_deliveries_.begin(),
                retired_deliveries_.end(),
                [&](const PendingDelivery& pending) {
                    return decoder_matches_delivery(
                        decoder_address, decoder_bytes, pending.identity);
                });
            if (retired != retired_deliveries_.end()) {
                // A verified byte-for-byte revisit of the displaced generation
                // is not a new transport delivery. Retain it as no proof;
                // accepting anything other than an exact retired identity
                // remains a hard protocol failure below.
                if (observation.wii_ticks <
                    retired->identity.delivery_wii_ticks) {
                    set_protocol_failure_locked(
                        NativeInputCausalityReason::DecoderChronologyInvalid);
                }
                return 0u;
            }
        }
        set_protocol_failure_locked(
            matching == pending_deliveries_.end()
                ? NativeInputCausalityReason::DecoderWithoutDelivery
                : NativeInputCausalityReason::DecoderReorderedDelivery);
        return 0u;
    }
    if (observation.wii_ticks < matching->identity.delivery_wii_ticks) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::DecoderChronologyInvalid);
        return 0u;
    }
    if (next_token_ == 0u ||
        next_decode_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::AllocationFailure);
        return 0u;
    }

    const Token token = next_token_++;
    const std::uint64_t decode_sequence = next_decode_sequence_++;
    try {
        decoder_frames_.push_back(DecoderFrame{
            token,
            decode_sequence,
            decoder_address,
            channel,
            observation,
            static_cast<std::uint32_t>(pending_deliveries_.size()),
            observation.wii_ticks -
                pending_deliveries_.front().identity.delivery_wii_ticks,
            matching->identity,
        });
    } catch (...) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::AllocationFailure);
        return 0u;
    }
    pending_deliveries_.pop_front();
    return token;
}

void NativeInputCausalityTracker::finish_report_decoder(
    Token token,
    std::uint32_t committed_wpad_slot_address,
    bool committed) noexcept {
    if (token == 0u) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (decoder_frames_.empty() || decoder_frames_.back().token != token) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::DecoderFrameMismatch);
        return;
    }
    const DecoderFrame frame = decoder_frames_.back();
    decoder_frames_.pop_back();
    if (!committed) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::DecoderDidNotCommit);
        return;
    }
    if (committed_wpad_slot_address < 0x80000000u ||
        committed_wpad_slot_address >= 0x94000000u) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::DecoderSlotInvalid);
        return;
    }
    try {
        wpad_slot_identities_.insert_or_assign(
            committed_wpad_slot_address,
            WpadSlotIdentity{
                frame.decode_sequence,
                frame.decoder_address,
                frame.channel,
                frame.observation,
                frame.pending_delivery_depth,
                frame.oldest_delivery_age_wii_ticks,
                frame.delivery,
            });
    } catch (...) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::AllocationFailure);
    }
}

void NativeInputCausalityTracker::abort_report_decoder(Token token) noexcept {
    if (token == 0u) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (decoder_frames_.empty() || decoder_frames_.back().token != token) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::DecoderFrameMismatch);
        return;
    }
    decoder_frames_.pop_back();
    set_protocol_failure_locked(
        NativeInputCausalityReason::DecoderDidNotCommit);
}

void NativeInputCausalityTracker::invalidate_wpad_control_slots(
    std::uint32_t wpad_control_block) noexcept {
    if (wpad_control_block < 0x80000000u ||
        wpad_control_block >= 0x94000000u) {
        return;
    }
    std::lock_guard lock(mutex_);
    wpad_slot_identities_.erase(wpad_control_block + 0xA0u);
    wpad_slot_identities_.erase(wpad_control_block + 0x100u);
}

void NativeInputCausalityTracker::observe_transport_generation(
    std::uint64_t epochs_started,
    std::uint64_t mode_invalidations,
    std::uint64_t disconnect_resets,
    bool armed) noexcept {
    std::lock_guard lock(mutex_);
    const bool generation_changed = have_transport_generation_ &&
        (epochs_started != transport_epochs_started_ ||
         mode_invalidations != transport_mode_invalidations_ ||
         disconnect_resets != transport_disconnect_resets_ ||
         (transport_armed_ && !armed));
    if (generation_changed || !armed) {
        // Once report-mode generation or transport ownership changes, an ACL
        // identity delivered under the previous generation cannot establish a
        // new decoder/output tag. Purge both queued and in-flight proof frames;
        // any translated call that was genuinely spanning the boundary will
        // later hard-fail its frame match instead of inheriting stale identity.
        pending_deliveries_.clear();
        retired_deliveries_.clear();
        decoder_frames_.clear();
        kpad_frames_.clear();
        kpad_read_frames_.clear();
        wpad_slot_identities_.clear();
        kpad_ring_identities_.clear();
        gameplay_output_identities_.clear();
    }
    have_transport_generation_ = true;
    transport_epochs_started_ = epochs_started;
    transport_mode_invalidations_ = mode_invalidations;
    transport_disconnect_resets_ = disconnect_resets;
    transport_armed_ = armed;
}

NativeInputCausalityTracker::Token
NativeInputCausalityTracker::begin_kpad_sample(std::uint32_t channel) noexcept {
    std::lock_guard lock(mutex_);
    if (next_token_ == 0u) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::AllocationFailure);
        return 0u;
    }
    const Token token = next_token_++;
    NativeInputCausalKpadSample sample{};
    sample.channel = channel;
    try {
        kpad_frames_.push_back(
            KpadFrame{token, channel, 0u, false, 0u, std::nullopt, sample});
    } catch (...) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::AllocationFailure);
        return 0u;
    }
    return token;
}

std::uint32_t NativeInputCausalityTracker::expected_copy_size(
    std::uint32_t call_pc) noexcept {
    switch (call_pc) {
    case 0x804D945Cu:
    case 0x804D94C4u:
        return 0x2Au;
    case 0x804D947Cu:
        return 0x32u;
    case 0x804D949Cu:
        return 0x36u;
    case 0x804D94B0u:
        return 0x5Au;
    default:
        return 0u;
    }
}

void NativeInputCausalityTracker::begin_wpad_read_copy(
    std::uint32_t call_pc,
    std::uint32_t destination,
    std::uint32_t source,
    std::uint32_t size) noexcept {
    std::lock_guard lock(mutex_);
    // WPADRead is also called outside KPAD sampling. Those copies are not a
    // KPAD causal observation and must not contaminate a later sample frame.
    if (kpad_frames_.empty()) {
        return;
    }
    KpadFrame& frame = kpad_frames_.back();
    ++frame.copy_count;
    if (is_rmge01_kpad_ring_destination(frame.channel, destination)) {
        frame.have_ring_destination = true;
        frame.ring_destination = destination;
        // Any observed write invalidates an older identity immediately. A
        // later exact finish re-tags the fully transformed ring entry.
        kpad_ring_identities_.erase(destination);
    }
    if (frame.copy_count != 1u) {
        frame.sample.exact = false;
        frame.sample.reason = NativeInputCausalityReason::KpadMultipleCopies;
        return;
    }
    frame.pending_copy = NativeInputWpadReadCopyArguments{
        call_pc, destination, source, size};
}

std::optional<NativeInputWpadReadCopyArguments>
NativeInputCausalityTracker::pending_wpad_read_copy(
    std::uint32_t call_pc) const noexcept {
    std::lock_guard lock(mutex_);
    if (kpad_frames_.empty() || !kpad_frames_.back().pending_copy.has_value() ||
        kpad_frames_.back().pending_copy->call_pc != call_pc) {
        return std::nullopt;
    }
    return kpad_frames_.back().pending_copy;
}

void NativeInputCausalityTracker::finish_wpad_read_copy(
    std::uint32_t call_pc,
    bool snapshot_equal,
    std::uint64_t copy_completion_wii_ticks) noexcept {
    std::lock_guard lock(mutex_);
    if (kpad_frames_.empty()) {
        return;
    }
    KpadFrame& frame = kpad_frames_.back();
    if (frame.copy_count != 1u) {
        frame.pending_copy.reset();
        frame.sample.exact = false;
        frame.sample.reason = NativeInputCausalityReason::KpadMultipleCopies;
        return;
    }
    if (!frame.pending_copy.has_value() ||
        frame.pending_copy->call_pc != call_pc) {
        frame.sample.exact = false;
        frame.sample.reason = NativeInputCausalityReason::KpadNoCopy;
        return;
    }
    const NativeInputWpadReadCopyArguments copy = *frame.pending_copy;
    frame.pending_copy.reset();
    if (protocol_failure_ != NativeInputCausalityReason::None) {
        frame.sample.reason = protocol_failure_;
        return;
    }
    const std::uint32_t required_size = expected_copy_size(copy.call_pc);
    if (required_size == 0u || copy.size != required_size) {
        frame.sample.reason =
            NativeInputCausalityReason::KpadCopySizeUnexpected;
        return;
    }
    if (!is_rmge01_kpad_ring_destination(frame.channel, copy.destination)) {
        frame.sample.reason =
            NativeInputCausalityReason::KpadDestinationUnexpected;
        return;
    }
    if (!snapshot_equal) {
        frame.sample.reason = NativeInputCausalityReason::KpadSnapshotMismatch;
        return;
    }
    const auto slot = wpad_slot_identities_.find(copy.source);
    if (slot == wpad_slot_identities_.end()) {
        frame.sample.reason = NativeInputCausalityReason::KpadSourceUntagged;
        return;
    }
    if (slot->second.channel != frame.channel) {
        frame.sample.reason = NativeInputCausalityReason::KpadChannelMismatch;
        return;
    }
    if (copy_completion_wii_ticks <
            slot->second.observation.wii_ticks ||
        copy_completion_wii_ticks <
            slot->second.delivery.delivery_wii_ticks) {
        frame.sample.reason =
            NativeInputCausalityReason::KpadChronologyInvalid;
        set_protocol_failure_locked(
            NativeInputCausalityReason::KpadChronologyInvalid);
        return;
    }

    frame.sample.exact = true;
    frame.sample.reason = NativeInputCausalityReason::None;
    frame.sample.delivery = slot->second.delivery;
    frame.sample.decode_sequence = slot->second.decode_sequence;
    frame.sample.decoder_address = slot->second.decoder_address;
    frame.sample.decoder_observation_wii_ticks =
        slot->second.observation.wii_ticks;
    frame.sample.decoder_entry_lr = slot->second.observation.entry_lr;
    frame.sample.decoder_current_thread =
        slot->second.observation.current_thread;
    frame.sample.decoder_current_context =
        slot->second.observation.current_context;
    frame.sample.decoder_pending_delivery_depth =
        slot->second.pending_delivery_depth;
    frame.sample.decoder_oldest_delivery_age_wii_ticks =
        slot->second.oldest_delivery_age_wii_ticks;
    frame.sample.wpad_slot_address = copy.source;
    frame.sample.wpad_copy_call_pc = copy.call_pc;
    frame.sample.wpad_copy_source = copy.source;
    frame.sample.kpad_copy_destination = copy.destination;
    frame.sample.copied_size = copy.size;
    frame.sample.kpad_copy_completion_wii_ticks =
        copy_completion_wii_ticks;
}

void NativeInputCausalityTracker::observe_wpad_read_copy(
    std::uint32_t call_pc,
    std::uint32_t destination,
    std::uint32_t source,
    std::uint32_t size,
    bool snapshot_equal,
    std::uint64_t copy_completion_wii_ticks) noexcept {
    begin_wpad_read_copy(call_pc, destination, source, size);
    finish_wpad_read_copy(
        call_pc, snapshot_equal, copy_completion_wii_ticks);
}

NativeInputCausalKpadSample
NativeInputCausalityTracker::finish_kpad_sample(Token token) noexcept {
    NativeInputCausalKpadSample result{};
    if (token == 0u) {
        result.reason = protocol_failure();
        if (result.reason == NativeInputCausalityReason::None) {
            result.reason = NativeInputCausalityReason::AllocationFailure;
        }
        return result;
    }
    std::lock_guard lock(mutex_);
    if (kpad_frames_.empty() || kpad_frames_.back().token != token) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::KpadFrameMismatch);
        result.reason = NativeInputCausalityReason::KpadFrameMismatch;
        return result;
    }
    KpadFrame frame = kpad_frames_.back();
    kpad_frames_.pop_back();
    result = frame.sample;
    if (protocol_failure_ != NativeInputCausalityReason::None) {
        result.exact = false;
        result.reason = protocol_failure_;
    } else if (frame.pending_copy.has_value()) {
        result.exact = false;
        result.reason = NativeInputCausalityReason::KpadAborted;
    } else if (frame.copy_count == 0u) {
        result.exact = false;
        result.reason = NativeInputCausalityReason::KpadNoCopy;
    }
    if (result.exact && frame.have_ring_destination) {
        try {
            kpad_ring_identities_.insert_or_assign(
                frame.ring_destination, result);
        } catch (...) {
            set_protocol_failure_locked(
                NativeInputCausalityReason::AllocationFailure);
            result.exact = false;
            result.reason = NativeInputCausalityReason::AllocationFailure;
        }
    }
    return result;
}

void NativeInputCausalityTracker::abort_kpad_sample(Token token) noexcept {
    if (token == 0u) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (kpad_frames_.empty() || kpad_frames_.back().token != token) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::KpadFrameMismatch);
        return;
    }
    kpad_frames_.pop_back();
}

void NativeInputCausalityTracker::begin_classified_kpad_read(
    std::uint32_t call_pc,
    std::uint32_t channel,
    std::uint32_t channel_base,
    std::uint32_t output_destination,
    std::uint32_t maximum_samples,
    std::uint32_t write_index,
    std::uint32_t queue_count,
    bool observation_interrupts_disabled,
    bool output_range_available,
    std::uint64_t observation_wii_ticks) noexcept {
    std::lock_guard lock(mutex_);
    NativeInputCausalKpadReadSample sample{};
    sample.call_pc = call_pc;
    sample.gameplay_read_data_route = call_pc == 0x803AB2B0u;
    sample.observation_interrupts_disabled =
        observation_interrupts_disabled;
    sample.output_range_available = output_range_available;
    sample.channel = channel;
    sample.observed_channel_base = channel_base;
    sample.output_destination = output_destination;
    sample.maximum_samples = maximum_samples;
    sample.observed_write_index = write_index;
    sample.observed_queue_count = queue_count;
    sample.read_observation_wii_ticks = observation_wii_ticks;

    // A new gameplay KPADRead owns this output range. Remove any older tag
    // before validating the protected source so failed/empty reads cannot
    // leave a stale identity for WPadPointer::update.
    if (sample.gameplay_read_data_route) {
        gameplay_output_identities_.erase(output_destination);
    }

    if (protocol_failure_ != NativeInputCausalityReason::None) {
        sample.reason = protocol_failure_;
    } else if (call_pc != 0x803AA570u && call_pc != 0x803AB2B0u) {
        sample.reason = NativeInputCausalityReason::KpadReadCallUnexpected;
    } else if (call_pc == 0x803AA570u) {
        if (channel >= 4u ||
            channel_base !=
                kRmge01KpadBase + channel * kRmge01KpadChannelStride ||
            maximum_samples != 1u) {
            sample.reason =
                NativeInputCausalityReason::KpadReadArgumentsInvalid;
        } else {
            // MR::getPadDataForExceptionNoInit is used by JUTException::readPad.
            // Observe it so exception-screen input can never be mistaken for
            // the normal WPadHolder gameplay path.
            sample.reason =
                NativeInputCausalityReason::KpadReadExceptionRouteClassified;
        }
    } else if (channel >= 4u ||
               channel_base !=
                   kRmge01KpadBase + channel * kRmge01KpadChannelStride ||
               maximum_samples != kRmge01KpadEntryCount ||
               output_destination < 0x80000000u ||
               output_destination > 0x93FFFF7Cu) {
        sample.reason =
            NativeInputCausalityReason::KpadReadArgumentsInvalid;
    } else if (!observation_interrupts_disabled) {
        sample.reason = NativeInputCausalityReason::KpadReadInterruptsEnabled;
    // KPADiSamplingCallback stores `bufIdx = index + 1` after writing its
    // clamped [0, 119] ring slot. Thus 120 is the valid one-past-last cursor
    // that KPADRead maps back to newest slot 119; only values above it are
    // corrupt guest state.
    } else if (write_index > kRmge01KpadEntryCount ||
               queue_count > kRmge01KpadEntryCount) {
        sample.reason =
            NativeInputCausalityReason::KpadReadQueueStateInvalid;
    } else if (queue_count == 0u) {
        sample.reason = NativeInputCausalityReason::KpadReadQueueEmpty;
    } else if (!output_range_available) {
        sample.reason = NativeInputCausalityReason::KpadReadOutputUnavailable;
    } else {
        // Generated KPADRead loads W while interrupts are disabled. Its
        // reverse output fill maps newest ring entry (W - 1) to status[0].
        const std::uint32_t source_index =
            (write_index + kRmge01KpadEntryCount - 1u) %
            kRmge01KpadEntryCount;
        sample.source_ring_entry = kRmge01KpadBase +
            channel * kRmge01KpadChannelStride + kRmge01KpadRingOffset +
            source_index * kRmge01KpadEntrySize;
        const auto identity =
            kpad_ring_identities_.find(sample.source_ring_entry);
        if (identity == kpad_ring_identities_.end()) {
            sample.reason =
                NativeInputCausalityReason::KpadReadSourceUntagged;
        } else if (observation_wii_ticks <
                   identity->second.kpad_copy_completion_wii_ticks) {
            sample.reason =
                NativeInputCausalityReason::KpadReadChronologyInvalid;
            set_protocol_failure_locked(
                NativeInputCausalityReason::KpadReadChronologyInvalid);
        } else {
            sample.exact = true;
            sample.reason = NativeInputCausalityReason::None;
            sample.ring_sample = identity->second;
        }
    }

    if (next_token_ == 0u) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::AllocationFailure);
        return;
    }
    const Token token = next_token_++;
    try {
        kpad_read_frames_.push_back(KpadReadFrame{token, sample});
    } catch (...) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::AllocationFailure);
    }
}

NativeInputCausalKpadReadSample
NativeInputCausalityTracker::finish_classified_kpad_read(
    std::uint32_t call_pc,
    std::uint32_t returned_samples,
    std::uint32_t caller_channel,
    std::uint32_t caller_output_destination,
    bool caller_output_available,
    std::uint64_t completion_wii_ticks) noexcept {
    std::lock_guard lock(mutex_);
    NativeInputCausalKpadReadSample result{};
    // Preserve the observed outer-call facts even for a rejected lifecycle.
    // The runtime's strict diagnostics must identify the actual return site,
    // rather than presenting default-zero fields for a frame mismatch.
    result.call_pc = call_pc;
    result.gameplay_read_data_route = call_pc == 0x803AB2B0u;
    result.returned_samples = returned_samples;
    result.channel = caller_channel;
    result.output_destination = caller_output_destination;
    result.output_range_available = caller_output_available;
    result.read_completion_wii_ticks = completion_wii_ticks;
    if (kpad_read_frames_.empty()) {
        // JUTException uses its own max=1 KPADRead wrapper.  It can return a
        // queued sample through KPADRead's early path before the common
        // OSDisableInterrupts snapshot is reached.  This diagnostic-only
        // route is never eligible to establish a WPadHolder pointer identity,
        // so classify it without converting a legitimate exception poll into
        // a global protocol failure.
        if (call_pc == 0x803AA570u) {
            result.reason =
                NativeInputCausalityReason::KpadReadExceptionRouteClassified;
            return result;
        }
        // A native interrupt can resume a translated caller at its generated
        // call-return label. That label deliberately replays the same
        // call_return_checkpoint before continuing the caller, so the one
        // protected KPADRead result appears at the host twice without a second
        // OSDisableInterrupts snapshot. Accept only the exact just-completed
        // WPadHolder output identity as this idempotent checkpoint replay; do
        // not create, replace, or erase any gameplay proof from the replay.
        if (protocol_failure_ == NativeInputCausalityReason::None &&
            call_pc == 0x803AB2B0u && returned_samples != 0u &&
            caller_output_available) {
            const auto completed = gameplay_output_identities_.find(
                caller_output_destination);
            if (completed != gameplay_output_identities_.end() &&
                completed->second.exact &&
                completed->second.gameplay_read_data_route &&
                completed->second.call_pc == call_pc &&
                completed->second.channel == caller_channel &&
                completed->second.output_destination ==
                    caller_output_destination &&
                completed->second.returned_samples == returned_samples &&
                completion_wii_ticks >=
                    completed->second.read_completion_wii_ticks) {
                result = completed->second;
                result.exact = false;
                result.returned_samples = returned_samples;
                result.read_completion_wii_ticks = completion_wii_ticks;
                result.reason = NativeInputCausalityReason::
                    KpadReadDuplicateReturnCheckpoint;
                return result;
            }
        }
        // KPADRead skips OSDisableInterrupts entirely when its queue is empty
        // (or an argument is null/zero), yet WPadHolder still reaches the outer
        // return for every channel. A zero return without a protected frame is
        // a legitimate non-proof classification, not tracker corruption.
        if (returned_samples == 0u && call_pc == 0x803AB2B0u) {
            result.reason =
                NativeInputCausalityReason::KpadReadNoProtectedSnapshot;
            if (result.gameplay_read_data_route) {
                if (caller_output_available) {
                    gameplay_output_identities_.erase(
                        caller_output_destination);
                } else {
                    gameplay_output_identities_.clear();
                }
            }
            return result;
        }
        set_protocol_failure_locked(
            NativeInputCausalityReason::KpadReadFrameMismatch);
        result.reason =
            NativeInputCausalityReason::KpadReadFrameMismatch;
        return result;
    }
    if (kpad_read_frames_.back().sample.call_pc != call_pc) {
        result.blocking_frame_call_pc =
            kpad_read_frames_.back().sample.call_pc;
        result.blocking_frame_depth = static_cast<std::uint32_t>(
            kpad_read_frames_.size());
        set_protocol_failure_locked(
            NativeInputCausalityReason::KpadReadFrameMismatch);
        result.reason =
            NativeInputCausalityReason::KpadReadFrameMismatch;
        return result;
    }
    result = kpad_read_frames_.back().sample;
    kpad_read_frames_.pop_back();
    if (result.gameplay_read_data_route) {
        if (caller_output_available) {
            gameplay_output_identities_.erase(caller_output_destination);
        } else {
            gameplay_output_identities_.clear();
        }
    }
    result.returned_samples = returned_samples;
    result.read_completion_wii_ticks = completion_wii_ticks;
    if (protocol_failure_ != NativeInputCausalityReason::None) {
        result.exact = false;
        result.reason = protocol_failure_;
    } else if (!result.gameplay_read_data_route) {
        result.exact = false;
        if (result.reason == NativeInputCausalityReason::None) {
            result.reason =
                NativeInputCausalityReason::KpadReadExceptionRouteClassified;
        }
    }
    if (!result.exact) {
        return result;
    }
    if (completion_wii_ticks < result.read_observation_wii_ticks) {
        set_protocol_failure_locked(
            NativeInputCausalityReason::KpadReadChronologyInvalid);
        result.exact = false;
        result.reason =
            NativeInputCausalityReason::KpadReadChronologyInvalid;
    } else if (returned_samples !=
               std::min(result.observed_queue_count,
                        result.maximum_samples)) {
        result.exact = false;
        result.reason =
            NativeInputCausalityReason::KpadReadReturnCountUnexpected;
    } else if (!result.output_range_available) {
        result.exact = false;
        result.reason =
            NativeInputCausalityReason::KpadReadOutputUnavailable;
    } else if (!caller_output_available ||
               caller_channel != result.channel ||
               caller_output_destination != result.output_destination) {
        result.exact = false;
        result.reason = NativeInputCausalityReason::KpadReadArgumentsInvalid;
    }
    if (result.exact) {
        try {
            gameplay_output_identities_.insert_or_assign(
                result.output_destination, result);
        } catch (...) {
            set_protocol_failure_locked(
                NativeInputCausalityReason::AllocationFailure);
            result.exact = false;
            result.reason = NativeInputCausalityReason::AllocationFailure;
        }
    }
    return result;
}

NativeInputCausalGameplayPointerSample
NativeInputCausalityTracker::observe_gameplay_pointer_status(
    std::uint32_t call_pc,
    std::uint32_t returned_status_pointer,
    bool status_range_available,
    std::uint64_t observation_wii_ticks) noexcept {
    std::lock_guard lock(mutex_);
    NativeInputCausalGameplayPointerSample result{};
    result.call_pc = call_pc;
    result.returned_status_pointer = returned_status_pointer;
    result.observation_wii_ticks = observation_wii_ticks;
    if (protocol_failure_ != NativeInputCausalityReason::None) {
        result.reason = protocol_failure_;
    } else if (call_pc != 0x803ABDF0u) {
        result.reason =
            NativeInputCausalityReason::GameplayPointerCallUnexpected;
    } else if (returned_status_pointer == 0u) {
        // WPadPointer probes every channel. Inactive channels correctly return
        // null instead of a KPADStatus pointer; they do not consume or replace
        // channel zero's gameplay output identity.
        result.reason =
            NativeInputCausalityReason::GameplayPointerStatusNull;
    } else if (!status_range_available) {
        result.reason =
            NativeInputCausalityReason::GameplayPointerStatusUnavailable;
    } else {
        const auto identity =
            gameplay_output_identities_.find(returned_status_pointer);
        if (identity == gameplay_output_identities_.end()) {
            result.reason =
                NativeInputCausalityReason::GameplayPointerStatusUntagged;
        } else if (observation_wii_ticks <
                   identity->second.read_completion_wii_ticks) {
            result.reason =
                NativeInputCausalityReason::GameplayPointerChronologyInvalid;
            set_protocol_failure_locked(
                NativeInputCausalityReason::GameplayPointerChronologyInvalid);
        } else {
            result.exact = true;
            result.reason = NativeInputCausalityReason::None;
            result.kpad_read = identity->second;
        }
    }
    return result;
}

NativeInputCausalityReason
NativeInputCausalityTracker::protocol_failure() const noexcept {
    std::lock_guard lock(mutex_);
    return protocol_failure_;
}

}  // namespace galaxy::input
