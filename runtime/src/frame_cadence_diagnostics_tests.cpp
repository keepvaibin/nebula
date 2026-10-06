#include "galaxy/frame_cadence_diagnostics.h"
#include "galaxy/main_frame_dynamic_trace.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

bool test_member_trace_nested_boundaries_and_transfer() {
    using galaxy::diagnostics::MainFrameDynamicCallRecord;
    using galaxy::diagnostics::append_main_frame_dynamic_record;
    std::array<MainFrameDynamicCallRecord, 8u> records{};
    std::size_t count = 0u;
    MainFrameDynamicCallRecord entry{};
    entry.call_pc = 0x80261B88u;
    entry.target = 0x80343D2Cu;
    entry.host_boundary = true;
    const auto host_serial = append_main_frame_dynamic_record(records, count, entry);
    entry.host_boundary = false;
    const auto body_serial = append_main_frame_dynamic_record(records, count, entry);
    entry.call_pc = 0x800C9AC4u;
    entry.target = 0x80443430u;
    const auto particle_member_serial =
        append_main_frame_dynamic_record(records, count, entry);
    MainFrameDynamicCallRecord returned = entry;
    returned.completed = true;
    returned.entry_serial = particle_member_serial;
    append_main_frame_dynamic_record(records, count, returned);
    returned.call_pc = 0x80261B88u;
    returned.target = 0x80343D2Cu;
    returned.entry_serial = body_serial;
    append_main_frame_dynamic_record(records, count, returned);
    returned.host_boundary = true;
    returned.entry_serial = host_serial;
    append_main_frame_dynamic_record(records, count, returned);
    // A later invocation may transfer instead of returning. Appending its
    // entry cannot silently complete either the earlier scope or itself.
    entry.vi = 5601u;
    const auto transfer_serial = append_main_frame_dynamic_record(records, count, entry);
    bool ok = true;
    ok &= expect(count == 7u && host_serial == 1u && body_serial == 2u &&
                     particle_member_serial == 3u && transfer_serial == 7u,
                 "nested boundary records have distinct ordered scope serials");
    ok &= expect(records[0].host_boundary && !records[1].host_boundary &&
                     !records[2].host_boundary && !records[3].host_boundary &&
                     !records[4].host_boundary && records[5].host_boundary &&
                     records[3].entry_serial == particle_member_serial &&
                     records[4].entry_serial == body_serial &&
                     records[5].entry_serial == host_serial,
                 "resolved host wrapper and guest body returns keep separate scope identities");
    ok &= expect(!records[6].completed && records[6].entry_serial == transfer_serial &&
                     !records[6].register_snapshot,
                 "unreturned transfer entry stays incomplete without invented snapshot");
    return ok;
}

bool test_member_trace_snapshot_and_overwritten_entry() {
    using galaxy::diagnostics::MainFrameDynamicCallRecord;
    using galaxy::diagnostics::append_main_frame_dynamic_record;
    // No memory or services member exists in this context: the actual
    // recorder helper can observe only registers, including unsigned bits.
    struct RegisterContext {
        std::array<std::uint32_t, 32u> gpr{};
        std::uint32_t ctr{};
    } context;
    for (std::size_t i = 0u; i < context.gpr.size(); ++i) {
        context.gpr[i] = 0xFFFFFFE0u + static_cast<std::uint32_t>(i);
    }
    context.ctr = 0x80343D2Cu;
    const auto original = context;
    MainFrameDynamicCallRecord entry{};
    galaxy::diagnostics::capture_member_entry_registers(entry, context);
    bool ok = expect(entry.register_snapshot && entry.entry_ctr == context.ctr &&
                         context.gpr == original.gpr && context.ctr == original.ctr,
                     "member snapshot preserves context and raw CTR");
    const std::array<std::uint32_t, 10u> expected{
        0xFFFFFFE0u, 0xFFFFFFE1u, 0xFFFFFFE3u, 0xFFFFFFE4u, 0xFFFFFFE5u,
        0xFFFFFFEBu, 0xFFFFFFECu, 0xFFFFFFFDu, 0xFFFFFFFEu, 0xFFFFFFFFu};
    ok &= expect(entry.entry_gpr == expected,
                 "snapshot register columns match emitted raw register labels");
    std::array<MainFrameDynamicCallRecord, 2u> records{};
    std::size_t count = 0u;
    const auto first = append_main_frame_dynamic_record(records, count, entry);
    append_main_frame_dynamic_record(records, count, entry);
    append_main_frame_dynamic_record(records, count, entry);
    MainFrameDynamicCallRecord returned{};
    returned.completed = true;
    returned.entry_serial = first;
    append_main_frame_dynamic_record(records, count, returned);
    ok &= expect(count == 4u && records[0].serial == 3u && records[1].serial == 4u &&
                     records[1].entry_serial == 1u && records[1].completed &&
                     !records[1].register_snapshot,
                 "wrapped ring keeps dangling return identity without stale entry registers");
    return ok;
}

std::uint64_t counting_clock(void* user) noexcept {
    auto& calls = *static_cast<std::uint64_t*>(user);
    ++calls;
    return 123'456u;
}


bool test_dsp_poll_origins_and_completed_scope_totals() {
    using galaxy::cadence::DspPollOrigin;
    using galaxy::cadence::TimingPhase;
    using galaxy::cadence::dsp_poll_timing_phase;
    auto session = std::make_unique<galaxy::cadence::Session>();
    const auto before = galaxy::cadence::snapshot_dsp_poll_timing(session.get());
    bool ok = expect(before.valid, "enabled DSP timing snapshot is marked measured");
    ok &= expect(
        dsp_poll_timing_phase(DspPollOrigin::CheckpointPreflight, false) ==
            TimingPhase::DspCheckpointPreflightPoll &&
        dsp_poll_timing_phase(DspPollOrigin::CheckpointPreflight, true) ==
            TimingPhase::DspCheckpointPreflightYield &&
        dsp_poll_timing_phase(DspPollOrigin::CheckpointPublication, false) ==
            TimingPhase::DspCheckpointPublicationPoll &&
        dsp_poll_timing_phase(DspPollOrigin::CheckpointPublication, true) ==
            TimingPhase::DspCheckpointPublicationYield,
        "checkpoint callers and their yield scopes have distinct counters");
    ok &= expect(
        dsp_poll_timing_phase(DspPollOrigin::Other, false) == TimingPhase::Count &&
        dsp_poll_timing_phase(DspPollOrigin::Other, true) == TimingPhase::Count,
        "other callers do not select a measured DSP phase");
    // Poll totals already include their yield. Distinct fabricated durations
    // check attribution and units without relying on operating-system timing.
    constexpr std::array<std::uint64_t, 4u> durations{
        100'000u, 200'000u, 10'000u, 20'000u};
    for (std::size_t index = 0u;
         index < galaxy::cadence::kDspPollTimingPhases.size(); ++index) {
        session->record_phase(
            galaxy::cadence::kDspPollTimingPhases[index], durations[index]);
    }
    const auto after = galaxy::cadence::snapshot_dsp_poll_timing(session.get());
    for (std::size_t index = 0u; index < durations.size(); ++index) {
        ok &= expect(
            after.counts[index] - before.counts[index] == 1u &&
            after.total_ns[index] - before.total_ns[index] == durations[index],
            "frame snapshots retain exact caller count and nanosecond deltas");
    }
    ok &= expect(
        session->phase_snapshot(TimingPhase::DspMramServiceWake).count == 0u,
        "poll counters do not change the separate MRAM-wake phase");
    std::ostringstream report;
    session->dump_report(report, "dsp-poll-unit-test");
    ok &= expect(
        report.str().find("name=dsp-checkpoint-preflight-poll count=1 total-ns=100000") !=
            std::string::npos &&
        report.str().find("name=dsp-checkpoint-publication-poll count=1 total-ns=200000") !=
            std::string::npos &&
        report.str().find("name=dsp-checkpoint-preflight-yield count=1 total-ns=10000") !=
            std::string::npos &&
        report.str().find("name=dsp-checkpoint-publication-yield count=1 total-ns=20000") !=
            std::string::npos,
        "deferred report retains all four named DSP counters");
    return ok;
}

bool test_dsp_poll_disabled_and_frame_record_retention() {
    auto disabled = std::make_unique<galaxy::cadence::Session>(false);
    std::uint64_t clock_calls = 0u;
    for (const auto phase : galaxy::cadence::kDspPollTimingPhases) {
        galaxy::cadence::ScopedPhaseTimer disabled_timer(disabled.get(), phase);
        galaxy::cadence::ScopedPhaseTimer null_timer(nullptr, phase);
        (void)galaxy::cadence::sample_now_ns_if_enabled(
            disabled.get(), &counting_clock, &clock_calls);
        (void)galaxy::cadence::sample_now_ns_if_enabled(
            nullptr, &counting_clock, &clock_calls);
        disabled->record_phase(phase, 1'000u);
    }
    const auto snapshot = galaxy::cadence::snapshot_dsp_poll_timing(disabled.get());
    const auto null_snapshot = galaxy::cadence::snapshot_dsp_poll_timing(nullptr);
    bool ok = expect(clock_calls == 0u, "disabled DSP sampling never invokes clock reader");
    ok &= expect(!snapshot.valid && !null_snapshot.valid,
                 "disabled and null frame snapshots do not claim measurement");
    for (std::size_t index = 0u; index < snapshot.counts.size(); ++index) {
        ok &= expect(
            snapshot.counts[index] == 0u && snapshot.total_ns[index] == 0u &&
            disabled->phase_snapshot(galaxy::cadence::kDspPollTimingPhases[index]).count == 0u,
            "disabled DSP timers and explicit recording leave counters untouched");
    }
    auto enabled = std::make_unique<galaxy::cadence::Session>();
    using galaxy::diagnostics::MainFrameDynamicCallRecord;
    std::array<MainFrameDynamicCallRecord, 2u> records{};
    std::size_t count = 0u;
    MainFrameDynamicCallRecord entry{};
    entry.dsp_poll_timing = galaxy::cadence::snapshot_dsp_poll_timing(enabled.get());
    const auto serial = galaxy::diagnostics::append_main_frame_dynamic_record(records, count, entry);
    enabled->record_phase(galaxy::cadence::TimingPhase::DspCheckpointPreflightPoll, 500u);
    MainFrameDynamicCallRecord returned{};
    returned.completed = true;
    returned.entry_serial = serial;
    returned.dsp_poll_timing = galaxy::cadence::snapshot_dsp_poll_timing(enabled.get());
    galaxy::diagnostics::append_main_frame_dynamic_record(records, count, returned);
    ok &= expect(
        records[0].dsp_poll_timing.total_ns[0] == 0u &&
        records[1].dsp_poll_timing.total_ns[0] == 500u &&
        records[1].entry_serial == records[0].serial,
        "paired frame entry and return own immutable cumulative snapshots");
    galaxy::diagnostics::append_main_frame_dynamic_record(records, count, entry);
    ok &= expect(
        records[1].dsp_poll_timing.total_ns[0] == 500u &&
        records[1].entry_serial == serial,
        "ring wrap preserves retained return totals without fabricating its lost entry");
    return ok;
}

bool test_ring_wrap_is_chronological() {
    auto session = std::make_unique<galaxy::cadence::Session>();
    const std::uint64_t total = galaxy::cadence::kViRecordCapacity + 7u;
    for (std::uint64_t sequence = 1u; sequence <= total; ++sequence) {
        galaxy::cadence::ViRecord record{};
        record.boundary_serial = sequence;
        session->push_vi_record(record);
    }

    bool ok = true;
    ok &= expect(
        session->total_vi_records() == total,
        "ring tracks total writes");
    ok &= expect(
        session->retained_vi_records() == galaxy::cadence::kViRecordCapacity,
        "ring retains fixed capacity");
    ok &= expect(
        session->overwritten_vi_records() == 7u,
        "ring reports overwritten records");
    ok &= expect(
        session->retained_vi_record(0).boundary_serial == 8u,
        "ring oldest retained record is chronological after wrap");
    ok &= expect(
        session->retained_vi_record(session->retained_vi_records() - 1u)
                .boundary_serial == total,
        "ring newest retained record survives wrap");
    return ok;
}

bool test_nanosecond_histogram_units_and_conservation() {
    auto session = std::make_unique<galaxy::cadence::Session>();
    const std::uint64_t values[]{
        0u,
        1'000u,
        1'001u,
        16'000'000u,
        16'000'001u,
        40'000'000u,
    };
    for (const std::uint64_t value : values) {
        session->record_phase(
            galaxy::cadence::TimingPhase::RenderFifoParse, value);
    }
    const auto snapshot = session->phase_snapshot(
        galaxy::cadence::TimingPhase::RenderFifoParse);
    const std::uint64_t bucket_total = std::accumulate(
        snapshot.buckets.begin(), snapshot.buckets.end(), std::uint64_t{0});

    bool ok = true;
    ok &= expect(snapshot.count == std::size(values), "phase sample count");
    ok &= expect(
        snapshot.total_ns == std::accumulate(
            std::begin(values), std::end(values), std::uint64_t{0}),
        "phase totals remain nanoseconds");
    ok &= expect(snapshot.max_ns == 40'000'000u, "phase max is exact");
    ok &= expect(bucket_total == snapshot.count, "histogram conserves count");
    ok &= expect(
        snapshot.buckets[0] == 2u,
        "inclusive one-microsecond bucket boundary");
    ok &= expect(
        snapshot.buckets.back() == 1u,
        "overflow bucket receives values above 33 milliseconds");
    return ok;
}

bool test_disabled_path_does_not_read_clock_or_mutate() {
    auto disabled =
        std::make_unique<galaxy::cadence::Session>(false);
    std::uint64_t clock_calls = 0u;
    const std::uint64_t sampled = galaxy::cadence::sample_now_ns_if_enabled(
        disabled.get(), &counting_clock, &clock_calls);
    disabled->record_phase(
        galaxy::cadence::TimingPhase::ProducerQueueWait, 99u);
    disabled->record_token_issued(
        galaxy::cadence::FrameTokenKind::EventBearing);
    disabled->record_consumer_completion(4u);
    disabled->push_vi_record(galaxy::cadence::ViRecord{});
    disabled->record_guest_checkpoint(
        99u,
        0x80001234u,
        0x40u,
        0x80400000u,
        true,
        true,
        false,
        1u,
        0x80401000u,
        2u,
        0u);
    disabled->record_inline_checkpoint_schedule(
        1u,
        galaxy::cadence::inline_checkpoint_schedule_cause_bit(
            galaxy::cadence::InlineCheckpointScheduleCause::DspDeadline));

    const auto phase = disabled->phase_snapshot(
        galaxy::cadence::TimingPhase::ProducerQueueWait);
    const auto tokens = disabled->token_snapshot();
    bool ok = true;
    ok &= expect(sampled == 0u, "disabled sample returns zero");
    ok &= expect(clock_calls == 0u, "disabled sample never calls clock");
    ok &= expect(phase.count == 0u, "disabled phase does not mutate atomics");
    ok &= expect(
        tokens.event_bearing_issued == 0u &&
            tokens.consumer_completed == 0u,
        "disabled token counters remain zero");
    ok &= expect(
        disabled->total_vi_records() == 0u,
        "disabled ring remains untouched");
    ok &= expect(
        disabled->guest_throughput_summary().samples == 0u,
        "disabled guest attribution remains untouched");
    ok &= expect(
        disabled->inline_checkpoint_schedule_summary().samples == 0u,
        "disabled inline schedule evidence remains untouched");
    return ok;
}

bool test_guest_throughput_attribution_is_bounded_and_exact() {
    auto session = std::make_unique<galaxy::cadence::Session>();
    session->record_guest_checkpoint(
        100u,
        0x809A00D8u,
        0u,
        0x803A0864u,
        false,
        false,
        true,
        0u,
        0u,
        0u,
        0u);
    session->record_guest_checkpoint(
        2'148u,
        0x809A00D8u,
        0u,
        0x803A0864u,
        true,
        true,
        false,
        2u,
        0x80401000u,
        3u,
        1u);
    session->record_guest_checkpoint(
        4'196u,
        0x809B2FD4u,
        0x20u,
        0x804AB360u,
        true,
        false,
        false,
        1u,
        0x80401000u,
        2u,
        2u);

    const auto summary = session->guest_throughput_summary();
    std::ostringstream output;
    session->dump_report(output, "guest-unit");
    const std::string report = output.str();

    bool ok = true;
    ok &= expect(summary.samples == 3u, "guest sample count is exact");
    ok &= expect(
        summary.checkpoint_delta == 4'096u,
        "guest checkpoint deltas conserve the observed interval");
    ok &= expect(
        summary.event_pending_samples == 2u &&
            summary.dsp_pending_samples == 1u,
        "guest event classifications are exact");
    ok &= expect(
        summary.inline_gate_samples == 1u &&
            summary.full_gate_samples == 2u &&
            summary.root_callback_samples == 1u &&
            summary.nested_callback_samples == 2u &&
            summary.root_full_gate_samples == 0u &&
            summary.nested_full_gate_samples == 2u,
        "guest gate and callback nesting classifications are exact");
    ok &= expect(
        summary.maximum_enclosing_callback_depth == 2u &&
            summary.maximum_gate_full_scope_depth == 3u &&
            summary.maximum_gate_inline_scope_depth == 2u,
        "guest gate nesting maxima are exact");
    ok &= expect(
        summary.thread_entries == 2u && summary.pc_entries == 2u &&
            summary.parent_entries == 2u,
        "guest attribution deduplicates fixed-table keys");
    ok &= expect(
        summary.thread_table_overflows == 0u &&
            summary.pc_table_overflows == 0u,
        "guest attribution does not overflow for bounded input");
    ok &= expect(
        report.find(
            "[guest-throughput-evidence] schema=4 tag=guest-unit samples=3 checkpoint-delta=4096") !=
            std::string::npos,
        "guest report carries the conserved summary");
    ok &= expect(
        report.find(
            "thread=0x809A00D8 runbits=0x0 samples=2 checkpoints=2048") !=
            std::string::npos,
        "guest report carries exact thread/run-queue attribution");
    ok &= expect(
        report.find(
            "pc=0x803A0864 samples=2 checkpoints=2048") !=
            std::string::npos,
        "guest report carries exact sampled-PC attribution");
    ok &= expect(
        report.find("parent-pc=0x80401000 samples=2") !=
            std::string::npos,
        "guest report carries exact enclosing-callback attribution");

    const std::uint32_t first_causes =
        galaxy::cadence::inline_checkpoint_schedule_cause_bit(
            galaxy::cadence::InlineCheckpointScheduleCause::DspDeadline) |
        galaxy::cadence::inline_checkpoint_schedule_cause_bit(
            galaxy::cadence::InlineCheckpointScheduleCause::AudioDeadline);
    session->record_inline_checkpoint_schedule(1u, first_causes);
    session->record_inline_checkpoint_schedule(
        2'048u,
        galaxy::cadence::inline_checkpoint_schedule_cause_bit(
            galaxy::cadence::InlineCheckpointScheduleCause::PeriodicViPoll));
    const auto schedule = session->inline_checkpoint_schedule_summary();
    ok &= expect(
        schedule.samples == 2u && schedule.gap_buckets[0] == 1u &&
            schedule.gap_buckets[5] == 1u,
        "inline checkpoint schedule gap buckets are exact");
    ok &= expect(
        schedule.cause_samples[static_cast<std::size_t>(
            galaxy::cadence::InlineCheckpointScheduleCause::DspDeadline)] ==
                1u &&
            schedule.cause_samples[static_cast<std::size_t>(
                galaxy::cadence::InlineCheckpointScheduleCause::AudioDeadline)] ==
                1u &&
            schedule.cause_samples[static_cast<std::size_t>(
                galaxy::cadence::InlineCheckpointScheduleCause::PeriodicViPoll)] ==
                1u,
        "inline checkpoint schedule overlapping causes are exact");

    session->reset();
    ok &= expect(
        session->guest_throughput_summary().samples == 0u,
        "guest attribution reset clears the measurement window");
    ok &= expect(
        session->inline_checkpoint_schedule_summary().samples == 0u,
        "inline schedule reset clears the measurement window");
    return ok;
}

bool test_report_schema_is_stable_and_complete() {
    auto session = std::make_unique<galaxy::cadence::Session>();
    galaxy::cadence::ViRecord record{};
    record.boundary_serial = 17u;
    record.deadline_sequence = 23u;
    record.deadline_ticks = 1'012'500u;
    record.deadline_actual_ticks = 1'012'700u;
    record.fifo_bytes = 4096u;
    record.xfb_copy_serial_before = 41u;
    record.xfb_copy_serial_after = 42u;
    record.selected_xfb_addr = 0x81234000u;
    record.token_kind = galaxy::cadence::FrameTokenKind::EventBearing;
    record.token_completed = true;
    session->push_vi_record(record);
    session->record_token_issued(record.token_kind);
    session->record_consumer_completion(3u);
    session->record_phase(
        galaxy::cadence::TimingPhase::LiveMemoryReadBarrierWait,
        2'500'000u);
    session->record_phase(
        galaxy::cadence::TimingPhase::EfbPeekSynchronization,
        1'500'000u);
    session->record_phase(
        galaxy::cadence::TimingPhase::EfbPeekReadback,
        500'000u);

    std::ostringstream output;
    session->dump_report(output, "unit-test");
    const std::string report = output.str();

    bool ok = true;
    ok &= expect(
        report.find(
            "[frame-cadence-evidence] schema=4 tag=unit-test time-unit=ns") !=
            std::string::npos,
        "report header identifies schema and units");
    ok &= expect(
        report.find("name=live-memory-read-barrier-wait count=1 ") !=
            std::string::npos,
        "report contains named phase totals");
    ok &= expect(
        report.find("name=efb-peek-synchronization count=1 total-ns=1500000") !=
            std::string::npos &&
        report.find("name=efb-peek-readback count=1 total-ns=500000") !=
            std::string::npos,
        "report keeps EFB synchronization and readback wall intervals separate");
    ok &= expect(
        report.find(
            "boundary=17 deadline-sequence=23 deadline-ticks=1012500") !=
            std::string::npos,
        "report contains exact VI identity");
    ok &= expect(
        report.find("copy-before=41 copy-after=42") != std::string::npos,
        "report contains copy-serial conservation fields");
    ok &= expect(
        report.find("token-kind=event-bearing") != std::string::npos,
        "report contains token classification");
    ok &= expect(
        report.find(
            "[frame-cadence-evidence-end] schema=4 tag=unit-test records=1") !=
            std::string::npos,
        "report has an explicit terminal schema record");
    const auto newline_count = static_cast<std::size_t>(
        std::count(report.begin(), report.end(), '\n'));
    ok &= expect(
        newline_count == galaxy::cadence::kTimingPhaseCount + 7u,
        "report emits cadence, guest-attribution, VI, and terminal records");
    return ok;
}

bool test_detached_vi_completion_has_exact_identity_and_no_wait() {
    auto session = std::make_unique<galaxy::cadence::Session>();
    galaxy::cadence::ViRecord record{};
    record.boundary_serial = 7u;
    record.token_epoch = 3u;
    record.token_value = 19u;
    record.token_detached = true;
    record.submit_end_ns = 100u;
    record.vi_latched_ns = 110u;
    record.finalize_ns = 130u;
    session->push_vi_record(record);
    bool ok = true;
    ok &= expect(!session->complete_detached_vi_token(7u, 4u, 19u, 200u) &&
                     !session->complete_detached_vi_token(7u, 3u, 19u, 99u),
                 "wrong epoch and pre-submission completion are rejected");
    ok &= expect(session->complete_detached_vi_token(7u, 3u, 19u, 200u),
                 "GPU readiness may be observed after exact IRQ24 RFI");
    const auto& completed = session->retained_vi_record(0u);
    ok &= expect(completed.token_completed && completed.token_complete_ns == 200u &&
                     completed.token_wait_ns == 0u &&
                     completed.vi_latched_ns == 110u && completed.finalize_ns == 130u,
                 "readiness observation neither fabricates wait time nor moves VI");
    ok &= expect(!session->complete_detached_vi_token(7u, 3u, 19u, 210u),
                 "same GPU receipt cannot complete twice");
    session->reset();
    ok &= expect(!session->complete_detached_vi_token(7u, 3u, 19u, 220u),
                 "reset receipt is missing explicitly, never recreated");
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_member_trace_nested_boundaries_and_transfer();
    ok &= test_member_trace_snapshot_and_overwritten_entry();
    ok &= test_ring_wrap_is_chronological();
    ok &= test_nanosecond_histogram_units_and_conservation();
    ok &= test_dsp_poll_origins_and_completed_scope_totals();
    ok &= test_dsp_poll_disabled_and_frame_record_retention();
    ok &= test_disabled_path_does_not_read_clock_or_mutate();
    ok &= test_guest_throughput_attribution_is_bounded_and_exact();
    ok &= test_report_schema_is_stable_and_complete();
    ok &= test_detached_vi_completion_has_exact_identity_and_no_wait();
    if (!ok) {
        return 1;
    }
    std::cout << "frame cadence diagnostics tests passed\n";
    return 0;
}
