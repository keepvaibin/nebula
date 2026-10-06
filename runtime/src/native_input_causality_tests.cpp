#include "galaxy/native_input_causality.h"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

std::uint64_t fingerprint(
    const galaxy::host::NativeVirtualInputAclDeliveryIdentity& identity) {
    constexpr std::uint64_t kOffset = 14'695'981'039'346'656'037ull;
    constexpr std::uint64_t kPrime = 1'099'511'628'211ull;
    std::uint64_t result = kOffset;
    for (std::uint32_t index = 0; index < identity.payload_size; ++index) {
        result ^= identity.payload[index];
        result *= kPrime;
    }
    return result;
}

galaxy::host::NativeVirtualInputAclDeliveryIdentity make_delivery(
    std::uint64_t epoch,
    std::uint64_t sample,
    std::uint32_t ios_request,
    std::uint8_t seed,
    std::uint8_t report_id = 0x37u) {
    galaxy::host::NativeVirtualInputAclDeliveryIdentity identity{};
    identity.logical_epoch = epoch;
    identity.sample_sequence = sample;
    identity.deadline_wii_ticks = 10'000u + sample * 4u;
    identity.production_wii_ticks = identity.deadline_wii_ticks + 1u;
    identity.delivery_wii_ticks = identity.deadline_wii_ticks + 2u;
    identity.host_pointer_sequence = 50'000u + sample;
    identity.host_pointer_acquired_ms = 70'000u + sample;
    identity.payload_size = report_id == 0x33u ? 19u : 23u;
    identity.payload[0] = 0xA1u;
    identity.payload[1] = report_id;
    for (std::uint32_t index = 2u; index < identity.payload_size; ++index) {
        identity.payload[index] =
            static_cast<std::uint8_t>(seed + index * 7u);
    }
    identity.ios_request = ios_request;
    identity.report_id = report_id;
    identity.host_pointer_sampled = true;
    identity.host_pointer_sequence_domain =
        galaxy::host::HostPointerSequenceDomain::AbsolutePosition;
    identity.payload_fingerprint = fingerprint(identity);
    return identity;
}

std::span<const std::uint8_t> decoder_bytes(
    const galaxy::host::NativeVirtualInputAclDeliveryIdentity& identity) {
    return {identity.payload.data() + 1u, identity.payload_size - 1u};
}

galaxy::input::NativeInputDecoderObservation decoder_observation(
    const galaxy::host::NativeVirtualInputAclDeliveryIdentity& identity,
    std::uint32_t lr = 0x81234560u) {
    return {
        identity.delivery_wii_ticks + 100u,
        lr,
        0x81000080u,
        0x810000C0u,
    };
}

constexpr std::uint64_t kCopyCompletionWiiTicks = 1'000'000u;

constexpr std::uint32_t kKpad0Destination = 0x8061D450u;

}  // namespace

int main() {
    bool passed = true;

    passed &= expect(
        galaxy::input::is_rmge01_kpad_ring_destination(
            0u, galaxy::input::kRmge01KpadBase +
                    galaxy::input::kRmge01KpadRingOffset) &&
            galaxy::input::is_rmge01_kpad_ring_destination(
                3u,
                galaxy::input::kRmge01KpadBase +
                    3u * galaxy::input::kRmge01KpadChannelStride +
                    galaxy::input::kRmge01KpadRingOffset +
                    (galaxy::input::kRmge01KpadEntryCount - 1u) *
                        galaxy::input::kRmge01KpadEntrySize) &&
            !galaxy::input::is_rmge01_kpad_ring_destination(
                0u, galaxy::input::kRmge01KpadBase +
                        galaxy::input::kRmge01KpadRingOffset + 1u) &&
            !galaxy::input::is_rmge01_kpad_ring_destination(
                0u,
                galaxy::input::kRmge01KpadBase +
                    galaxy::input::kRmge01KpadRingOffset +
                    galaxy::input::kRmge01KpadEntryCount *
                        galaxy::input::kRmge01KpadEntrySize),
        "source-derived KPAD ring geometry accepts only aligned in-range entries for all channels");

    passed &= expect(
        std::string_view(galaxy::input::native_input_causality_reason_name(
            galaxy::input::NativeInputCausalityReason::DecoderReorderedDelivery)) ==
            "decoder-reordered-delivery",
        "causality failures have stable machine-readable labels");

    {
        const auto absolute = make_delivery(6u, 1u, 0x0FF0u, 0x08u);
        auto transition = absolute;
        transition.host_pointer_sequence_domain =
            galaxy::host::HostPointerSequenceDomain::ButtonTransition;
        passed &= expect(
            absolute.host_pointer_sequence == transition.host_pointer_sequence &&
                absolute != transition,
            "equal numeric host pointer sequences from separate producer domains remain distinct ACL identities");
    }

    {
        // A real guest memmove may clobber r3-r5 before its generated
        // call-return checkpoint. The proof must retain only the exact
        // pre-call operands and evaluate byte equality at completion.
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto delivery = make_delivery(6u, 2u, 0x0FF4u, 0x09u);
        tracker.observe_acl_delivery(delivery);
        const auto decode = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(delivery),
            decoder_observation(delivery));
        tracker.finish_report_decoder(decode, 0x81000040u, true);
        const auto kpad = tracker.begin_kpad_sample(0u);
        tracker.begin_wpad_read_copy(
            0x804D94B0u,
            kKpad0Destination,
            0x81000040u,
            0x5Au);
        const auto pending = tracker.pending_wpad_read_copy(0x804D94B0u);
        tracker.finish_wpad_read_copy(
            0x804D94B0u, true, kCopyCompletionWiiTicks);
        const auto result = tracker.finish_kpad_sample(kpad);
        passed &= expect(
            pending.has_value() &&
                pending->destination == kKpad0Destination &&
                pending->source == 0x81000040u &&
                pending->size == 0x5Au && result.exact &&
                result.delivery == delivery,
            "split pre/post WPAD copy observation preserves pre-call operands and requires post-copy evidence");
    }

    {
        // Three delivered identities share one nominal VI deadline. The gap
        // from sample 100 to 103 models FIFO replacement before delivery;
        // only reports that actually cross IOS appear in this queue.
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto first = make_delivery(7u, 100u, 0x1000u, 0x10u);
        const auto second = make_delivery(7u, 101u, 0x1004u, 0x20u);
        const auto after_replacement =
            make_delivery(7u, 103u, 0x1008u, 0x30u);
        tracker.observe_acl_delivery(first);
        tracker.observe_acl_delivery(second);
        tracker.observe_acl_delivery(after_replacement);

        const auto decode_first = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(first),
            decoder_observation(first));
        tracker.finish_report_decoder(decode_first, 0x81000100u, true);
        const auto decode_second = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(second),
            decoder_observation(second));
        tracker.finish_report_decoder(decode_second, 0x81000160u, true);
        const auto decode_third = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(after_replacement),
            decoder_observation(after_replacement));
        tracker.finish_report_decoder(decode_third, 0x81000100u, true);

        const auto kpad = tracker.begin_kpad_sample(0u);
        tracker.observe_wpad_read_copy(
            0x804D94B0u,
            kKpad0Destination,
            0x81000100u,
            0x5Au,
            true,
            kCopyCompletionWiiTicks);
        const auto result = tracker.finish_kpad_sample(kpad);
        passed &= expect(
            result.exact && result.delivery == after_replacement &&
                result.decode_sequence == 3u &&
                result.wpad_slot_address == 0x81000100u &&
                result.kpad_copy_destination == kKpad0Destination,
            "multiple deliveries per VI and a replacement gap retain the exact newest committed identity");
        passed &= expect(
            tracker.protocol_failure() ==
                galaxy::input::NativeInputCausalityReason::None,
            "a missing pre-delivery sequence is a valid FIFO replacement, not fabricated delivery");
    }

    {
        // Byte-identical reports remain distinct identities. FIFO decode order
        // maps the first and second copies to separate alternating WPAD slots.
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto first = make_delivery(9u, 1u, 0x2000u, 0x44u);
        auto duplicate = first;
        duplicate.sample_sequence = 2u;
        duplicate.deadline_wii_ticks += 4u;
        duplicate.production_wii_ticks += 4u;
        duplicate.delivery_wii_ticks += 4u;
        duplicate.host_pointer_sequence += 1u;
        duplicate.host_pointer_acquired_ms += 1u;
        duplicate.ios_request = 0x2004u;
        tracker.observe_acl_delivery(first);
        tracker.observe_acl_delivery(duplicate);

        auto token = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(first),
            decoder_observation(first));
        tracker.finish_report_decoder(token, 0x81000200u, true);
        token = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(duplicate),
            decoder_observation(duplicate));
        tracker.finish_report_decoder(token, 0x81000260u, true);

        auto kpad = tracker.begin_kpad_sample(0u);
        tracker.observe_wpad_read_copy(
            0x804D945Cu,
            kKpad0Destination,
            0x81000200u,
            0x2Au,
            true,
            kCopyCompletionWiiTicks);
        const auto first_result = tracker.finish_kpad_sample(kpad);
        kpad = tracker.begin_kpad_sample(0u);
        tracker.observe_wpad_read_copy(
            0x804D945Cu,
            kKpad0Destination + 0x38u,
            0x81000260u,
            0x2Au,
            true,
            kCopyCompletionWiiTicks + 1u);
        const auto duplicate_result = tracker.finish_kpad_sample(kpad);
        passed &= expect(
            first_result.exact && duplicate_result.exact &&
                first_result.delivery.ios_request == 0x2000u &&
                duplicate_result.delivery.ios_request == 0x2004u &&
                first_result.delivery.sample_sequence == 1u &&
                duplicate_result.delivery.sample_sequence == 2u,
            "duplicate payload bytes are attributed by ordered delivery identity, not by hash equality");
    }

    {
        // A new cadence epoch may reset its sequence while an old-epoch report
        // is still waiting to be decoded. The boundary purges the stale report,
        // then the first new-epoch delivery starts a fresh exact route.
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto old_epoch = make_delivery(12u, 800u, 0x3000u, 0x51u);
        const auto reset_epoch = make_delivery(13u, 0u, 0x3004u, 0x61u);
        tracker.observe_acl_delivery(old_epoch);
        tracker.observe_acl_delivery(reset_epoch);
        auto token = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(reset_epoch),
            decoder_observation(reset_epoch));
        tracker.finish_report_decoder(token, 0x81000360u, true);
        const auto kpad = tracker.begin_kpad_sample(0u);
        tracker.observe_wpad_read_copy(
            0x804D949Cu,
            kKpad0Destination,
            0x81000360u,
            0x36u,
            true,
            kCopyCompletionWiiTicks);
        const auto result = tracker.finish_kpad_sample(kpad);
        passed &= expect(
            result.exact && result.delivery.logical_epoch == 13u &&
                result.delivery.sample_sequence == 0u,
            "reset epochs purge stale pending delivery and propagate the first reset identity exactly");
    }

    {
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto first = make_delivery(20u, 1u, 0x4000u, 0x70u);
        const auto second = make_delivery(20u, 2u, 0x4004u, 0x80u);
        tracker.observe_acl_delivery(first);
        tracker.observe_acl_delivery(second);
        const auto token = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(second),
            decoder_observation(second));
        passed &= expect(
            token == 0u &&
                tracker.protocol_failure() ==
                    galaxy::input::NativeInputCausalityReason::DecoderReorderedDelivery,
            "a decoder that skips an earlier distinct delivery is rejected as reordered");
    }

    {
        // RMGE01 may re-enter a report decoder to revisit the status already
        // stored in guest WPAD memory after the matching bulk-in report was
        // consumed. With no new ACL delivery there is no identity to prove,
        // but this must not poison the next independently exact report.
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto delivery = make_delivery(21u, 1u, 0x4100u, 0x71u);
        tracker.observe_acl_delivery(delivery);
        auto token = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(delivery),
            decoder_observation(delivery));
        tracker.finish_report_decoder(token, 0x810003A0u, true);
        token = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(delivery),
            decoder_observation(delivery));
        passed &= expect(
            token == 0u &&
                tracker.protocol_failure() ==
                    galaxy::input::NativeInputCausalityReason::None,
            "a decoder revisit with no queued ACL delivery is untracked, not a false transport failure");
    }

    {
        galaxy::input::NativeInputCausalityTracker tracker;
        auto invalid = make_delivery(30u, 1u, 0x5000u, 0x90u);
        invalid.payload_fingerprint ^= 1u;
        tracker.observe_acl_delivery(invalid);
        passed &= expect(
            tracker.protocol_failure() ==
                galaxy::input::NativeInputCausalityReason::InvalidDeliveryFingerprint,
            "a delivery hash that does not match the exact A1 bytes is rejected");
    }

    {
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto delivery = make_delivery(40u, 1u, 0x6000u, 0xA0u);
        tracker.observe_acl_delivery(delivery);
        const auto decode = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(delivery),
            decoder_observation(delivery));
        tracker.finish_report_decoder(decode, 0x81000400u, true);

        auto kpad = tracker.begin_kpad_sample(0u);
        tracker.observe_wpad_read_copy(
            0x804D94B0u,
            kKpad0Destination,
            0x81000400u,
            0x5Au,
            false,
            kCopyCompletionWiiTicks);
        auto result = tracker.finish_kpad_sample(kpad);
        passed &= expect(
            !result.exact &&
                result.reason ==
                    galaxy::input::NativeInputCausalityReason::KpadSnapshotMismatch,
            "source-to-destination identity requires a measured equal post-copy snapshot");

        kpad = tracker.begin_kpad_sample(0u);
        tracker.observe_wpad_read_copy(
            0x804D94B0u,
            kKpad0Destination,
            0x81000400u,
            0x5Au,
            true,
            kCopyCompletionWiiTicks);
        tracker.observe_wpad_read_copy(
            0x804D94B0u,
            kKpad0Destination + 0x38u,
            0x81000400u,
            0x5Au,
            true,
            kCopyCompletionWiiTicks);
        result = tracker.finish_kpad_sample(kpad);
        passed &= expect(
            !result.exact &&
                result.reason ==
                    galaxy::input::NativeInputCausalityReason::KpadMultipleCopies,
            "one KPAD observation cannot silently choose between multiple WPAD copies");

        kpad = tracker.begin_kpad_sample(0u);
        tracker.observe_wpad_read_copy(
            0x804D94B0u,
            kKpad0Destination,
            0x81000BADu,
            0x5Au,
            true,
            kCopyCompletionWiiTicks);
        result = tracker.finish_kpad_sample(kpad);
        passed &= expect(
            !result.exact &&
                result.reason ==
                    galaxy::input::NativeInputCausalityReason::KpadSourceUntagged,
            "an untagged WPAD source slot is reported as ambiguous");
    }

    {
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto delivery = make_delivery(50u, 2u, 0x7000u, 0xB0u);
        tracker.observe_acl_delivery(delivery);
        const auto token = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(delivery),
            decoder_observation(delivery));
        tracker.finish_report_decoder(token, 0x81000500u, false);
        passed &= expect(
            tracker.protocol_failure() ==
                galaxy::input::NativeInputCausalityReason::DecoderDidNotCommit,
            "a consumed report that does not commit exactly one WPAD slot makes later attribution ambiguous");
    }

    {
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto delivery = make_delivery(60u, 1u, 0x8000u, 0xC0u);
        tracker.observe_acl_delivery(delivery);
        const auto decode = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(delivery),
            decoder_observation(delivery));
        tracker.finish_report_decoder(decode, 0x810006A0u, true);

        // Models any later entry in the complete 0x20..0x3F report dispatch
        // table. Entry invalidation occurs before that decoder can reuse the
        // alternating slots, even if its bytes happen to match the old status.
        tracker.invalidate_wpad_control_slots(0x81000600u);
        const auto kpad = tracker.begin_kpad_sample(0u);
        tracker.observe_wpad_read_copy(
            0x804D945Cu,
            kKpad0Destination,
            0x810006A0u,
            0x2Au,
            true,
            kCopyCompletionWiiTicks);
        const auto result = tracker.finish_kpad_sample(kpad);
        passed &= expect(
            !result.exact &&
                result.reason ==
                    galaxy::input::NativeInputCausalityReason::KpadSourceUntagged,
            "a non-IR or unmatched decoder cannot inherit a stale IR slot identity");
    }

    {
        // RMGE01 configures its WPAD report state before the first native
        // virtual ACL report can be delivered.  That startup decoder has no
        // transport identity, so it must be untracked rather than poisoning
        // the later, fully observed native stream.  Once the first delivery
        // arrives, the same tracker must resume exact attribution.
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto bootstrap = make_delivery(69u, 1u, 0x8FFCu, 0xCFu);
        const auto bootstrap_decode = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(bootstrap),
            decoder_observation(bootstrap));
        passed &= expect(
            bootstrap_decode == 0u &&
                tracker.protocol_failure() ==
                    galaxy::input::NativeInputCausalityReason::None,
            "a pre-transport-generation WPAD bootstrap decoder is untracked rather than assigned a fabricated ACL identity");

        tracker.observe_transport_generation(1u, 0u, 0u, true);
        tracker.observe_acl_delivery(bootstrap);
        const auto tracked_decode = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(bootstrap),
            decoder_observation(bootstrap));
        tracker.finish_report_decoder(tracked_decode, 0x81000760u, true);
        passed &= expect(
            tracked_decode != 0u &&
                tracker.protocol_failure() ==
                    galaxy::input::NativeInputCausalityReason::None,
            "the first actual native ACL delivery immediately enables exact decoder attribution");
    }

    {
        galaxy::input::NativeInputCausalityTracker tracker;
        tracker.observe_transport_generation(1u, 0u, 0u, true);
        const auto delivery = make_delivery(70u, 1u, 0x9000u, 0xD0u);
        tracker.observe_acl_delivery(delivery);
        const auto decode = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(delivery),
            decoder_observation(delivery));
        tracker.finish_report_decoder(decode, 0x810007A0u, true);

        tracker.observe_transport_generation(2u, 1u, 0u, true);
        auto kpad = tracker.begin_kpad_sample(0u);
        tracker.observe_wpad_read_copy(
            0x804D945Cu,
            kKpad0Destination,
            0x810007A0u,
            0x2Au,
            true,
            kCopyCompletionWiiTicks);
        auto result = tracker.finish_kpad_sample(kpad);
        passed &= expect(
            !result.exact &&
                result.reason ==
                    galaxy::input::NativeInputCausalityReason::KpadSourceUntagged,
            "a report-mode/reset epoch invalidates every pre-epoch WPAD slot tag");

        const auto after_reset = make_delivery(71u, 0u, 0x9004u, 0xD1u);
        tracker.observe_acl_delivery(after_reset);
        const auto reset_decode = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(after_reset),
            decoder_observation(after_reset));
        tracker.finish_report_decoder(reset_decode, 0x810007A0u, true);
        tracker.observe_transport_generation(2u, 1u, 1u, false);
        kpad = tracker.begin_kpad_sample(0u);
        tracker.observe_wpad_read_copy(
            0x804D945Cu,
            kKpad0Destination,
            0x810007A0u,
            0x2Au,
            true,
            kCopyCompletionWiiTicks);
        result = tracker.finish_kpad_sample(kpad);
        passed &= expect(
            !result.exact &&
                result.reason ==
                    galaxy::input::NativeInputCausalityReason::KpadSourceUntagged,
            "disconnect/disarm invalidates a previously exact post-reset slot identity");
    }

    {
        galaxy::input::NativeInputCausalityTracker tracker;
        tracker.observe_transport_generation(1u, 0u, 0u, true);
        const auto stale = make_delivery(75u, 1u, 0x9800u, 0xD8u);
        tracker.observe_acl_delivery(stale);
        tracker.observe_transport_generation(2u, 1u, 0u, true);
        const auto stale_decode = tracker.begin_report_decoder(
            0x804E0ED0u,
            0u,
            decoder_bytes(stale),
            decoder_observation(stale));
        passed &= expect(
            stale_decode == 0u &&
                tracker.protocol_failure() ==
                    galaxy::input::NativeInputCausalityReason::None,
            "a delivered-but-undecoded ACL identity cannot cross a report-mode generation boundary or fabricate a new identity");
    }

    {
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto old_epoch = make_delivery(76u, 1u, 0x9900u, 0xD9u);
        const auto new_epoch = make_delivery(77u, 0u, 0x9904u, 0xDAu);
        tracker.observe_acl_delivery(old_epoch);
        tracker.observe_acl_delivery(new_epoch);
        const auto stale_decode = tracker.begin_report_decoder(
            0x804E0ED0u,
            0u,
            decoder_bytes(old_epoch),
            decoder_observation(old_epoch));
        passed &= expect(
            stale_decode == 0u &&
                tracker.protocol_failure() ==
                    galaxy::input::NativeInputCausalityReason::None,
            "the first delivery of a new epoch immediately purges undecoded identities from the old epoch without poisoning a later exact route");
        const auto new_decode = tracker.begin_report_decoder(
            0x804E0ED0u,
            0u,
            decoder_bytes(new_epoch),
            decoder_observation(new_epoch));
        passed &= expect(
            new_decode != 0u &&
                tracker.protocol_failure() ==
                    galaxy::input::NativeInputCausalityReason::None,
            "an exact retired-generation revisit leaves the new FIFO identity available for strict decoding");

        galaxy::input::NativeInputCausalityTracker unknown_tracker;
        unknown_tracker.observe_acl_delivery(old_epoch);
        unknown_tracker.observe_acl_delivery(new_epoch);
        const auto unknown = make_delivery(76u, 2u, 0x9908u, 0xDBu);
        const auto unknown_decode = unknown_tracker.begin_report_decoder(
            0x804E0ED0u,
            0u,
            decoder_bytes(unknown),
            decoder_observation(unknown));
        passed &= expect(
            unknown_decode == 0u &&
                unknown_tracker.protocol_failure() ==
                    galaxy::input::NativeInputCausalityReason::
                        DecoderWithoutDelivery,
            "a post-boundary decoder must exactly match the displaced FIFO; unrelated bytes still hard-fail");
    }

    {
        // Recursive call_guest dispatch is host-stack nested. The explicit
        // frame vectors must therefore attribute an inner callback first and
        // then resume the outer frame without cross-contamination.
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto outer_delivery = make_delivery(80u, 1u, 0xA000u, 0xE0u);
        const auto inner_delivery = make_delivery(80u, 2u, 0xA004u, 0xE1u);
        tracker.observe_acl_delivery(outer_delivery);
        tracker.observe_acl_delivery(inner_delivery);
        auto decode = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(outer_delivery),
            decoder_observation(outer_delivery));
        tracker.finish_report_decoder(decode, 0x810008A0u, true);
        decode = tracker.begin_report_decoder(
            0x804E0ED0u, 1u, decoder_bytes(inner_delivery),
            decoder_observation(inner_delivery));
        tracker.finish_report_decoder(decode, 0x810009A0u, true);

        const auto outer = tracker.begin_kpad_sample(0u);
        const auto inner = tracker.begin_kpad_sample(1u);
        tracker.observe_wpad_read_copy(
            0x804D945Cu,
            kKpad0Destination + galaxy::input::kRmge01KpadChannelStride,
            0x810009A0u,
            0x2Au,
            true,
            kCopyCompletionWiiTicks);
        const auto inner_result = tracker.finish_kpad_sample(inner);
        tracker.observe_wpad_read_copy(
            0x804D945Cu,
            kKpad0Destination + 0x38u,
            0x810008A0u,
            0x2Au,
            true,
            kCopyCompletionWiiTicks + 1u);
        const auto outer_result = tracker.finish_kpad_sample(outer);
        passed &= expect(
            inner_result.exact && outer_result.exact &&
                inner_result.delivery == inner_delivery &&
                outer_result.delivery == outer_delivery,
            "nested KPAD proof frames are strictly LIFO under recursive native guest calls");
    }

    {
        // KPADiSamplingCallback stores bufIdx as the just-written slot plus
        // one. After writing slot 119 it intentionally exposes 120 until the
        // next callback wraps the producer cursor to zero. KPADRead's
        // `bufIdx - copy_ct` arithmetic therefore maps cursor 120, count 1
        // back to the final physical ring slot.
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto delivery = make_delivery(89u, 1u, 0xAFF0u, 0x30u);
        tracker.observe_acl_delivery(delivery);
        const auto decode = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(delivery),
            decoder_observation(delivery));
        tracker.finish_report_decoder(decode, 0x81000A80u, true);
        const auto ring = tracker.begin_kpad_sample(0u);
        constexpr std::uint32_t kFinalRingDestination =
            kKpad0Destination +
            (galaxy::input::kRmge01KpadEntryCount - 1u) *
                galaxy::input::kRmge01KpadEntrySize;
        tracker.observe_wpad_read_copy(
            0x804D945Cu,
            kFinalRingDestination,
            0x81000A80u,
            0x2Au,
            true,
            kCopyCompletionWiiTicks);
        static_cast<void>(tracker.finish_kpad_sample(ring));
        tracker.begin_classified_kpad_read(
            0x803AB2B0u,
            0u,
            galaxy::input::kRmge01KpadBase,
            0x81230008u,
            galaxy::input::kRmge01KpadEntryCount,
            galaxy::input::kRmge01KpadEntryCount,
            1u,
            true,
            true,
            kCopyCompletionWiiTicks + 10u);
        const auto wrapped = tracker.finish_classified_kpad_read(
            0x803AB2B0u,
            1u,
            0u,
            0x81230008u,
            true,
            kCopyCompletionWiiTicks + 20u);
        passed &= expect(
            wrapped.exact &&
                wrapped.reason == galaxy::input::NativeInputCausalityReason::None &&
                wrapped.observed_write_index ==
                    galaxy::input::kRmge01KpadEntryCount &&
                wrapped.source_ring_entry == kFinalRingDestination &&
                wrapped.ring_sample.delivery == delivery &&
                tracker.protocol_failure() ==
                    galaxy::input::NativeInputCausalityReason::None,
            "KPADRead accepts the valid one-past-last producer cursor and maps it to ring slot 119");
    }

    {
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto delivery = make_delivery(90u, 1u, 0xB000u, 0x31u);
        tracker.observe_acl_delivery(delivery);
        auto observation = decoder_observation(delivery, 0x804F1234u);
        observation.current_thread = 0x81230080u;
        observation.current_context = 0x81230100u;
        const auto decode = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(delivery), observation);
        tracker.finish_report_decoder(decode, 0x81000AA0u, true);

        const auto ring = tracker.begin_kpad_sample(0u);
        tracker.observe_wpad_read_copy(
            0x804D945Cu,
            kKpad0Destination,
            0x81000AA0u,
            0x2Au,
            true,
            observation.wii_ticks + 40u);
        const auto ring_result = tracker.finish_kpad_sample(ring);

        tracker.begin_classified_kpad_read(
            0x803AB2B0u,
            0u,
            galaxy::input::kRmge01KpadBase,
            0x81231008u,
            galaxy::input::kRmge01KpadEntryCount,
            1u,
            1u,
            true,
            true,
            observation.wii_ticks + 80u);
        const auto gameplay = tracker.finish_classified_kpad_read(
            0x803AB2B0u,
            1u,
            0u,
            0x81231008u,
            true,
            observation.wii_ticks + 90u);
        // An interrupt may resume the generated caller through its interior
        // call-return label, which deliberately invokes the same checkpoint
        // again without rerunning KPADRead. The duplicate return must not
        // destroy the exact output identity from the protected invocation.
        const auto duplicate = tracker.finish_classified_kpad_read(
            0x803AB2B0u,
            1u,
            0u,
            0x81231008u,
            true,
            observation.wii_ticks + 95u);
        const auto pointer = tracker.observe_gameplay_pointer_status(
            0x803ABDF0u,
            0x81231008u,
            true,
            observation.wii_ticks + 100u);
        const auto inactive_channel_pointer =
            tracker.observe_gameplay_pointer_status(
                0x803ABDF0u,
                0u,
                false,
                observation.wii_ticks + 110u);
        passed &= expect(
            ring_result.exact && gameplay.exact && !duplicate.exact &&
                duplicate.reason ==
                    galaxy::input::NativeInputCausalityReason::
                        KpadReadDuplicateReturnCheckpoint &&
                tracker.protocol_failure() ==
                    galaxy::input::NativeInputCausalityReason::None &&
                pointer.exact &&
                !inactive_channel_pointer.exact &&
                inactive_channel_pointer.reason ==
                    galaxy::input::NativeInputCausalityReason::
                        GameplayPointerStatusNull &&
                gameplay.gameplay_read_data_route &&
                pointer.kpad_read.output_destination == 0x81231008u &&
                gameplay.ring_sample.delivery == delivery &&
                gameplay.ring_sample.decoder_observation_wii_ticks ==
                    observation.wii_ticks &&
                gameplay.ring_sample.decoder_entry_lr == observation.entry_lr &&
                gameplay.ring_sample.decoder_current_thread ==
                    observation.current_thread &&
                gameplay.ring_sample.decoder_current_context ==
                    observation.current_context &&
                gameplay.ring_sample.decoder_pending_delivery_depth == 1u &&
                gameplay.ring_sample.decoder_oldest_delivery_age_wii_ticks ==
                    observation.wii_ticks - delivery.delivery_wii_ticks &&
                gameplay.source_ring_entry == kKpad0Destination &&
                gameplay.observation_interrupts_disabled &&
                gameplay.read_completion_wii_ticks ==
                    observation.wii_ticks + 90u &&
                pointer.observation_wii_ticks == observation.wii_ticks + 100u,
             "one clock and exact protected ring identity propagate through WPadHolder output[0] while inactive channels remain non-consuming");

        tracker.begin_classified_kpad_read(
            0x803AA570u,
            0u,
            galaxy::input::kRmge01KpadBase,
            0x81232000u,
            1u,
            1u,
            1u,
            true,
            true,
            observation.wii_ticks + 100u);
        const auto exception = tracker.finish_classified_kpad_read(
            0x803AA570u,
            1u,
            0u,
            0x81232000u,
            true,
            observation.wii_ticks + 110u);
        passed &= expect(
            !exception.exact && !exception.gameplay_read_data_route &&
                exception.call_pc == 0x803AA570u &&
                exception.reason == galaxy::input::NativeInputCausalityReason::
                                        KpadReadExceptionRouteClassified,
            "the max=1 JUTException reader is classified and cannot satisfy normal gameplay pointer proof");

        // The exception wrapper can take KPADRead's early-return path before
        // the shared OSDisableInterrupts snapshot.  Its nonzero result must
        // remain a diagnostic-only exception classification rather than
        // globally poisoning the independent WPadHolder pointer route.
        const auto early_exception = tracker.finish_classified_kpad_read(
            0x803AA570u,
            1u,
            0u,
            0x81232000u,
            true,
            observation.wii_ticks + 111u);
        passed &= expect(
            !early_exception.exact &&
                !early_exception.gameplay_read_data_route &&
                early_exception.call_pc == 0x803AA570u &&
                early_exception.returned_samples == 1u &&
                early_exception.reason ==
                    galaxy::input::NativeInputCausalityReason::
                        KpadReadExceptionRouteClassified &&
                tracker.protocol_failure() ==
                    galaxy::input::NativeInputCausalityReason::None,
            "an early nonzero JUTException read remains classified without corrupting WPadHolder causality");

        // WPadHolder calls KPADRead for all four channels. Empty channels skip
        // the protected block and return zero; those returns are legitimate
        // non-proof records and must not poison channel zero's exact route.
        for (std::uint32_t channel = 1u; channel < 4u; ++channel) {
            const auto empty = tracker.finish_classified_kpad_read(
                0x803AB2B0u,
                0u,
                channel,
                0x81231008u + channel * 0x1000u,
                true,
                observation.wii_ticks + 110u + channel);
            passed &= expect(
                !empty.exact && empty.gameplay_read_data_route &&
                    empty.reason == galaxy::input::NativeInputCausalityReason::
                                        KpadReadNoProtectedSnapshot,
                "empty WPadHolder channels are classified without a false frame mismatch");
        }
        passed &= expect(
            tracker.protocol_failure() ==
                galaxy::input::NativeInputCausalityReason::None,
            "one exact plus three empty WPadHolder channels preserve tracker integrity");

        const auto queued_delivery = make_delivery(90u, 2u, 0xB004u, 0x32u);
        tracker.observe_acl_delivery(queued_delivery);
        const auto queued_decode = tracker.begin_report_decoder(
            0x804E0ED0u,
            0u,
            decoder_bytes(queued_delivery),
            decoder_observation(queued_delivery));
        tracker.finish_report_decoder(queued_decode, 0x81000B00u, true);
        const auto queued_ring = tracker.begin_kpad_sample(0u);
        constexpr std::uint32_t kNewestQueuedDestination =
            kKpad0Destination + 3u * galaxy::input::kRmge01KpadEntrySize;
        tracker.observe_wpad_read_copy(
            0x804D945Cu,
            kNewestQueuedDestination,
            0x81000B00u,
            0x2Au,
            true,
            observation.wii_ticks + 120u);
        static_cast<void>(tracker.finish_kpad_sample(queued_ring));
        tracker.begin_classified_kpad_read(
            0x803AB2B0u,
            0u,
            galaxy::input::kRmge01KpadBase,
            0x81235000u,
            galaxy::input::kRmge01KpadEntryCount,
            4u,
            3u,
            true,
            true,
            observation.wii_ticks + 130u);
        const auto queued = tracker.finish_classified_kpad_read(
            0x803AB2B0u,
            3u,
            0u,
            0x81235000u,
            true,
            observation.wii_ticks + 140u);
        passed &= expect(
            queued.exact && queued.maximum_samples == 0x78u &&
                queued.returned_samples == 3u &&
                queued.source_ring_entry == kNewestQueuedDestination,
            "gameplay proof accepts a multi-entry queue and maps W-1 to output[0] without assuming max=1");
    }

    {
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto delivery = make_delivery(91u, 1u, 0xB100u, 0x41u);
        tracker.observe_acl_delivery(delivery);
        auto invalid_observation = decoder_observation(delivery);
        invalid_observation.wii_ticks = delivery.delivery_wii_ticks - 1u;
        const auto token = tracker.begin_report_decoder(
            0x804E0ED0u,
            0u,
            decoder_bytes(delivery),
            invalid_observation);
        passed &= expect(
            token == 0u &&
                tracker.protocol_failure() ==
                    galaxy::input::NativeInputCausalityReason::
                        DecoderChronologyInvalid,
            "decoder observations earlier than their exact IOS delivery hard-fail the causal proof");
    }

    {
        // A trace checkpoint can coincide with a real pending event. If a
        // nested sampling callback overwrites the chosen ring slot before the
        // outer KPADRead returns, the captured identity must be rejected.
        galaxy::input::NativeInputCausalityTracker tracker;
        const auto first = make_delivery(92u, 1u, 0xB200u, 0x51u);
        const auto second = make_delivery(92u, 2u, 0xB204u, 0x61u);
        tracker.observe_acl_delivery(first);
        tracker.observe_acl_delivery(second);
        auto decode = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(first),
            decoder_observation(first));
        tracker.finish_report_decoder(decode, 0x81000BA0u, true);
        decode = tracker.begin_report_decoder(
            0x804E0ED0u, 0u, decoder_bytes(second),
            decoder_observation(second));
        tracker.finish_report_decoder(decode, 0x81000C00u, true);

        auto ring = tracker.begin_kpad_sample(0u);
        tracker.observe_wpad_read_copy(
            0x804D945Cu,
            kKpad0Destination,
            0x81000BA0u,
            0x2Au,
            true,
            kCopyCompletionWiiTicks);
        static_cast<void>(tracker.finish_kpad_sample(ring));
        tracker.begin_classified_kpad_read(
            0x803AB2B0u,
            0u,
            galaxy::input::kRmge01KpadBase,
            0x81233008u,
            galaxy::input::kRmge01KpadEntryCount,
            1u,
            1u,
            true,
            true,
            kCopyCompletionWiiTicks + 10u);

        ring = tracker.begin_kpad_sample(0u);
        tracker.observe_wpad_read_copy(
            0x804D945Cu,
            kKpad0Destination,
            0x81000C00u,
            0x2Au,
            true,
            kCopyCompletionWiiTicks + 20u);
        static_cast<void>(tracker.finish_kpad_sample(ring));
        const auto transferred = tracker.finish_classified_kpad_read(
            0x803AB2B0u,
            1u,
            0u,
            0x81233008u,
            true,
            kCopyCompletionWiiTicks + 30u);
        const auto pointer = tracker.observe_gameplay_pointer_status(
            0x803ABDF0u,
            0x81233008u,
            true,
            kCopyCompletionWiiTicks + 40u);
        passed &= expect(
            transferred.exact && pointer.exact &&
                transferred.ring_sample.delivery == first &&
                pointer.kpad_read.ring_sample.delivery == first,
            "a legitimate ring reuse after KPADRead's protected copy does not erase the identity transferred to gameplay output[0]");
    }

    if (!passed) {
        return 1;
    }
    std::cout << "native input causality tests passed\n";
    return 0;
}
