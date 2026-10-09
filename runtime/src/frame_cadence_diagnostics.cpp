#include "galaxy/frame_cadence_diagnostics.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <vector>

namespace galaxy::cadence {
namespace {

Session g_global_session{true};
std::atomic<Session*> g_active_session{nullptr};
constexpr std::size_t kMaxAttributionProbes = 64u;

void update_max(
    std::atomic_uint64_t& destination,
    std::uint64_t candidate) noexcept {
    std::uint64_t current = destination.load(std::memory_order_relaxed);
    while (current < candidate &&
           !destination.compare_exchange_weak(
               current,
               candidate,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

std::uint64_t steady_clock_read(void*) noexcept {
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0u;
}

void saturating_increment(std::uint64_t& value) noexcept {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

void saturating_add(
    std::uint64_t& value,
    std::uint64_t addend) noexcept {
    if (addend > std::numeric_limits<std::uint64_t>::max() - value) {
        value = std::numeric_limits<std::uint64_t>::max();
    } else {
        value += addend;
    }
}

std::uint64_t attribution_hash(std::uint64_t key) noexcept {
    key ^= key >> 33u;
    key *= UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 33u;
    key *= UINT64_C(0xc4ceb9fe1a85ec53);
    key ^= key >> 33u;
    return key;
}

}  // namespace

void TimingAccumulator::record(std::uint64_t elapsed_ns) noexcept {
    count_.fetch_add(1u, std::memory_order_relaxed);
    total_ns_.fetch_add(elapsed_ns, std::memory_order_relaxed);
    update_max(max_ns_, elapsed_ns);
    buckets_[timing_histogram_bucket_for_ns(elapsed_ns)].fetch_add(
        1u, std::memory_order_relaxed);
}

void TimingAccumulator::reset() noexcept {
    count_.store(0u, std::memory_order_relaxed);
    total_ns_.store(0u, std::memory_order_relaxed);
    max_ns_.store(0u, std::memory_order_relaxed);
    for (auto& bucket : buckets_) {
        bucket.store(0u, std::memory_order_relaxed);
    }
}

TimingSnapshot TimingAccumulator::snapshot() const noexcept {
    TimingSnapshot result{};
    result.count = count_.load(std::memory_order_relaxed);
    result.total_ns = total_ns_.load(std::memory_order_relaxed);
    result.max_ns = max_ns_.load(std::memory_order_relaxed);
    for (std::size_t index = 0; index < result.buckets.size(); ++index) {
        result.buckets[index] =
            buckets_[index].load(std::memory_order_relaxed);
    }
    return result;
}

void Session::reset() noexcept {
    total_vi_records_ = 0u;
    for (auto& phase : phases_) {
        phase.reset();
    }
    event_free_issued_.store(0u, std::memory_order_relaxed);
    event_bearing_issued_.store(0u, std::memory_order_relaxed);
    consumer_completed_.store(0u, std::memory_order_relaxed);
    consumer_wait_slices_.store(0u, std::memory_order_relaxed);
    guest_thread_attribution_.fill({});
    guest_pc_attribution_.fill({});
    guest_parent_attribution_.fill({});
    guest_throughput_ = {};
    inline_checkpoint_schedule_ = {};
    last_guest_checkpoint_ = 0u;
    last_guest_checkpoint_valid_ = false;
}

void Session::push_vi_record(const ViRecord& record) noexcept {
    if (!enabled_) {
        return;
    }
    const std::size_t slot = static_cast<std::size_t>(
        total_vi_records_ % kViRecordCapacity);
    vi_records_[slot] = record;
    if (total_vi_records_ != std::numeric_limits<std::uint64_t>::max()) {
        ++total_vi_records_;
    }
}

std::size_t Session::retained_vi_records() const noexcept {
    return static_cast<std::size_t>(
        std::min<std::uint64_t>(total_vi_records_, kViRecordCapacity));
}

std::uint64_t Session::overwritten_vi_records() const noexcept {
    return total_vi_records_ > kViRecordCapacity
        ? total_vi_records_ - kViRecordCapacity
        : 0u;
}

const ViRecord& Session::retained_vi_record(
    std::size_t chronological_index) const {
    const std::size_t retained = retained_vi_records();
    if (chronological_index >= retained) {
        throw std::out_of_range("VI cadence record index is out of range");
    }
    const std::uint64_t first_absolute =
        total_vi_records_ - static_cast<std::uint64_t>(retained);
    const std::uint64_t absolute =
        first_absolute + static_cast<std::uint64_t>(chronological_index);
    return vi_records_[static_cast<std::size_t>(absolute % kViRecordCapacity)];
}

bool Session::complete_detached_vi_token(
    std::uint64_t serial, std::uint64_t epoch, std::uint64_t value,
    std::uint64_t completed_ns) noexcept {
    if (!enabled_ || serial == 0u || epoch == 0u || value == 0u ||
        completed_ns == 0u) return false;
    const auto retained = retained_vi_records();
    for (std::size_t age = 0; age < retained; ++age) {
        const auto absolute = total_vi_records_ - 1u - age;
        auto& record = vi_records_[
            static_cast<std::size_t>(absolute % kViRecordCapacity)];
        if (record.boundary_serial != serial) continue;
        if (!record.token_detached || record.token_completed ||
            record.token_epoch != epoch || record.token_value != value ||
            completed_ns < record.submit_end_ns) return false;
        record.token_completed = true;
        record.token_complete_ns = completed_ns;
        return true;
    }
    return false;
}

void Session::record_phase(
    TimingPhase phase,
    std::uint64_t elapsed_ns) noexcept {
    if (!enabled_) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(phase);
    if (index < phases_.size()) {
        phases_[index].record(elapsed_ns);
    }
}

TimingSnapshot Session::phase_snapshot(TimingPhase phase) const noexcept {
    const std::size_t index = static_cast<std::size_t>(phase);
    return index < phases_.size() ? phases_[index].snapshot()
                                  : TimingSnapshot{};
}

void Session::record_token_issued(FrameTokenKind kind) noexcept {
    if (!enabled_) {
        return;
    }
    if (kind == FrameTokenKind::EventFree) {
        event_free_issued_.fetch_add(1u, std::memory_order_relaxed);
    } else if (kind == FrameTokenKind::EventBearing) {
        event_bearing_issued_.fetch_add(1u, std::memory_order_relaxed);
    }
}

void Session::record_consumer_completion(std::uint32_t wait_slices) noexcept {
    if (!enabled_) {
        return;
    }
    consumer_completed_.fetch_add(1u, std::memory_order_relaxed);
    consumer_wait_slices_.fetch_add(wait_slices, std::memory_order_relaxed);
}

TokenSnapshot Session::token_snapshot() const noexcept {
    return TokenSnapshot{
        event_free_issued_.load(std::memory_order_relaxed),
        event_bearing_issued_.load(std::memory_order_relaxed),
        consumer_completed_.load(std::memory_order_relaxed),
        consumer_wait_slices_.load(std::memory_order_relaxed),
    };
}

void Session::record_guest_checkpoint(
    std::uint64_t checkpoint,
    std::uint32_t current_thread,
    std::uint32_t run_queue_bits,
    std::uint32_t guest_pc,
    bool event_pending,
    bool dsp_pending,
    bool inline_gate_published,
    std::uint64_t enclosing_callback_depth,
    std::uint32_t parent_guest_pc,
    std::uint64_t gate_full_scope_depth,
    std::uint64_t gate_inline_scope_depth) noexcept {
    if (!enabled_) {
        return;
    }

    std::uint64_t delta = 0u;
    if (last_guest_checkpoint_valid_) {
        if (checkpoint >= last_guest_checkpoint_) {
            delta = checkpoint - last_guest_checkpoint_;
        } else {
            saturating_increment(guest_throughput_.checkpoint_regressions);
        }
    }
    last_guest_checkpoint_ = checkpoint;
    last_guest_checkpoint_valid_ = true;
    saturating_increment(guest_throughput_.samples);
    saturating_add(guest_throughput_.checkpoint_delta, delta);
    if (event_pending) {
        saturating_increment(guest_throughput_.event_pending_samples);
    }
    if (dsp_pending) {
        saturating_increment(guest_throughput_.dsp_pending_samples);
    }
    if (inline_gate_published) {
        saturating_increment(guest_throughput_.inline_gate_samples);
    } else {
        saturating_increment(guest_throughput_.full_gate_samples);
    }
    if (enclosing_callback_depth == 0u) {
        saturating_increment(guest_throughput_.root_callback_samples);
        if (!inline_gate_published) {
            saturating_increment(guest_throughput_.root_full_gate_samples);
        }
    } else {
        saturating_increment(guest_throughput_.nested_callback_samples);
        if (!inline_gate_published) {
            saturating_increment(guest_throughput_.nested_full_gate_samples);
        }
    }
    guest_throughput_.maximum_enclosing_callback_depth = std::max(
        guest_throughput_.maximum_enclosing_callback_depth,
        enclosing_callback_depth);
    guest_throughput_.maximum_gate_full_scope_depth = std::max(
        guest_throughput_.maximum_gate_full_scope_depth,
        gate_full_scope_depth);
    guest_throughput_.maximum_gate_inline_scope_depth = std::max(
        guest_throughput_.maximum_gate_inline_scope_depth,
        gate_inline_scope_depth);

    const std::uint64_t thread_key =
        (static_cast<std::uint64_t>(current_thread) << 32u) |
        run_queue_bits;
    const std::size_t thread_mask = guest_thread_attribution_.size() - 1u;
    bool thread_recorded = false;
    for (std::size_t probe = 0u;
         probe < std::min(guest_thread_attribution_.size(), kMaxAttributionProbes);
         ++probe) {
        auto& slot = guest_thread_attribution_[
            (static_cast<std::size_t>(attribution_hash(thread_key)) + probe) &
            thread_mask];
        if (!slot.occupied) {
            slot.occupied = true;
            slot.thread = current_thread;
            slot.run_queue_bits = run_queue_bits;
            saturating_increment(guest_throughput_.thread_entries);
        }
        if (slot.thread == current_thread &&
            slot.run_queue_bits == run_queue_bits) {
            saturating_increment(slot.samples);
            saturating_add(slot.checkpoints, delta);
            thread_recorded = true;
            break;
        }
    }
    if (!thread_recorded) {
        saturating_increment(guest_throughput_.thread_table_overflows);
    }

    const std::size_t pc_mask = guest_pc_attribution_.size() - 1u;
    bool pc_recorded = false;
    for (std::size_t probe = 0u; probe < std::min(guest_pc_attribution_.size(), kMaxAttributionProbes); ++probe) {
        auto& slot = guest_pc_attribution_[
            (static_cast<std::size_t>(attribution_hash(guest_pc)) + probe) &
            pc_mask];
        if (!slot.occupied) {
            slot.occupied = true;
            slot.pc = guest_pc;
            saturating_increment(guest_throughput_.pc_entries);
        }
        if (slot.pc == guest_pc) {
            saturating_increment(slot.samples);
            saturating_add(slot.checkpoints, delta);
            pc_recorded = true;
            break;
        }
    }
    if (!pc_recorded) {
        saturating_increment(guest_throughput_.pc_table_overflows);
    }

    const std::size_t parent_mask = guest_parent_attribution_.size() - 1u;
    bool parent_recorded = false;
    for (std::size_t probe = 0u;
         probe < std::min(guest_parent_attribution_.size(), kMaxAttributionProbes);
         ++probe) {
        auto& slot = guest_parent_attribution_[
            (static_cast<std::size_t>(attribution_hash(parent_guest_pc)) +
             probe) & parent_mask];
        if (!slot.occupied) {
            slot.occupied = true;
            slot.parent_pc = parent_guest_pc;
            saturating_increment(guest_throughput_.parent_entries);
        }
        if (slot.parent_pc == parent_guest_pc) {
            saturating_increment(slot.samples);
            parent_recorded = true;
            break;
        }
    }
    if (!parent_recorded) {
        saturating_increment(guest_throughput_.parent_table_overflows);
    }
}

GuestThroughputSummary Session::guest_throughput_summary() const noexcept {
    return guest_throughput_;
}

void Session::record_inline_checkpoint_schedule(
    std::uint64_t scheduled_gap,
    std::uint32_t limiting_cause_mask) noexcept {
    if (!enabled_) {
        return;
    }
    saturating_increment(inline_checkpoint_schedule_.samples);
    saturating_increment(
        inline_checkpoint_schedule_.gap_buckets[
            inline_checkpoint_gap_bucket(scheduled_gap)]);
    if (limiting_cause_mask == 0u) {
        saturating_increment(
            inline_checkpoint_schedule_.unattributed_samples);
    }
    for (std::size_t index = 0u;
         index < inline_checkpoint_schedule_.cause_samples.size();
         ++index) {
        if ((limiting_cause_mask & (std::uint32_t{1u} << index)) != 0u) {
            saturating_increment(
                inline_checkpoint_schedule_.cause_samples[index]);
        }
    }
}

InlineCheckpointScheduleSummary
Session::inline_checkpoint_schedule_summary() const noexcept {
    return inline_checkpoint_schedule_;
}

void Session::dump_report(std::ostream& output, const char* tag) const {
    if (!enabled_) {
        return;
    }
    const char* const safe_tag = tag != nullptr ? tag : "unknown";
    output << "[frame-cadence-evidence] schema=" << kReportSchemaVersion
           << " tag=" << safe_tag
           << " time-unit=ns"
           << " ring-capacity=" << kViRecordCapacity
           << " records-total=" << total_vi_records()
           << " records-retained=" << retained_vi_records()
           << " records-overwritten=" << overwritten_vi_records() << '\n';

    const TokenSnapshot tokens = token_snapshot();
    output << "[frame-cadence-token] tag=" << safe_tag
           << " event-free-issued=" << tokens.event_free_issued
           << " event-bearing-issued=" << tokens.event_bearing_issued
           << " consumer-completed=" << tokens.consumer_completed
           << " consumer-wait-slices=" << tokens.consumer_wait_slices
           << '\n';

    for (std::size_t index = 0; index < kTimingPhaseCount; ++index) {
        const auto phase = static_cast<TimingPhase>(index);
        const TimingSnapshot timing = phase_snapshot(phase);
        output << "[frame-cadence-phase] tag=" << safe_tag
               << " name=" << timing_phase_name(phase)
               << " count=" << timing.count
               << " total-ns=" << timing.total_ns
               << " max-ns=" << timing.max_ns
               << " buckets=";
        for (std::size_t bucket = 0; bucket < timing.buckets.size(); ++bucket) {
            if (bucket != 0u) {
                output << ',';
            }
            output << timing.buckets[bucket];
        }
        output << '\n';
    }

    const GuestThroughputSummary guest = guest_throughput_summary();
    output << "[guest-throughput-evidence] schema=" << kReportSchemaVersion
           << " tag=" << safe_tag
           << " samples=" << guest.samples
           << " checkpoint-delta=" << guest.checkpoint_delta
           << " event-pending-samples=" << guest.event_pending_samples
           << " dsp-pending-samples=" << guest.dsp_pending_samples
           << " inline-gate-samples=" << guest.inline_gate_samples
           << " full-gate-samples=" << guest.full_gate_samples
           << " root-callback-samples=" << guest.root_callback_samples
           << " nested-callback-samples=" << guest.nested_callback_samples
           << " root-full-gate-samples=" << guest.root_full_gate_samples
           << " nested-full-gate-samples=" << guest.nested_full_gate_samples
           << " maximum-enclosing-callback-depth="
           << guest.maximum_enclosing_callback_depth
           << " maximum-gate-full-scope-depth="
           << guest.maximum_gate_full_scope_depth
           << " maximum-gate-inline-scope-depth="
           << guest.maximum_gate_inline_scope_depth
           << " checkpoint-regressions=" << guest.checkpoint_regressions
           << " thread-entries=" << guest.thread_entries
           << " thread-table-overflows=" << guest.thread_table_overflows
           << " pc-entries=" << guest.pc_entries
           << " pc-table-overflows=" << guest.pc_table_overflows
           << " attribution-max-probes=" << kMaxAttributionProbes
           << " attribution-overflow=omitted-sampled-events"
           << " parent-entries=" << guest.parent_entries
           << " parent-table-overflows=" << guest.parent_table_overflows
           << '\n';

    std::vector<GuestThreadAttribution> thread_attribution;
    thread_attribution.reserve(static_cast<std::size_t>(guest.thread_entries));
    for (const auto& slot : guest_thread_attribution_) {
        if (slot.occupied) {
            thread_attribution.push_back(slot);
        }
    }
    std::sort(
        thread_attribution.begin(),
        thread_attribution.end(),
        [](const auto& left, const auto& right) {
            if (left.checkpoints != right.checkpoints) {
                return left.checkpoints > right.checkpoints;
            }
            return left.samples > right.samples;
        });
    for (const auto& item : thread_attribution) {
        output << "[guest-throughput-thread] tag=" << safe_tag
               << " thread=0x" << std::hex << std::uppercase
               << item.thread
               << " runbits=0x" << item.run_queue_bits
               << std::dec << std::nouppercase
               << " samples=" << item.samples
               << " checkpoints=" << item.checkpoints << '\n';
    }

    std::vector<GuestPcAttribution> pc_attribution;
    pc_attribution.reserve(static_cast<std::size_t>(guest.pc_entries));
    for (const auto& slot : guest_pc_attribution_) {
        if (slot.occupied) {
            pc_attribution.push_back(slot);
        }
    }
    std::sort(
        pc_attribution.begin(),
        pc_attribution.end(),
        [](const auto& left, const auto& right) {
            if (left.checkpoints != right.checkpoints) {
                return left.checkpoints > right.checkpoints;
            }
            return left.samples > right.samples;
        });
    constexpr std::size_t kPcReportLimit = 128u;
    const std::size_t pc_report_count =
        std::min(pc_attribution.size(), kPcReportLimit);
    for (std::size_t index = 0u; index < pc_report_count; ++index) {
        const auto& item = pc_attribution[index];
        output << "[guest-throughput-pc] tag=" << safe_tag
               << " pc=0x" << std::hex << std::uppercase << item.pc
               << std::dec << std::nouppercase
               << " samples=" << item.samples
               << " checkpoints=" << item.checkpoints << '\n';
    }

    std::vector<GuestParentAttribution> parent_attribution;
    parent_attribution.reserve(
        static_cast<std::size_t>(guest.parent_entries));
    for (const auto& slot : guest_parent_attribution_) {
        if (slot.occupied) {
            parent_attribution.push_back(slot);
        }
    }
    std::sort(
        parent_attribution.begin(),
        parent_attribution.end(),
        [](const auto& left, const auto& right) {
            return left.samples > right.samples;
        });
    constexpr std::size_t kParentReportLimit = 64u;
    const std::size_t parent_report_count =
        std::min(parent_attribution.size(), kParentReportLimit);
    for (std::size_t index = 0u; index < parent_report_count; ++index) {
        const auto& item = parent_attribution[index];
        output << "[guest-throughput-parent] tag=" << safe_tag
               << " parent-pc=0x" << std::hex << std::uppercase
               << item.parent_pc
               << std::dec << std::nouppercase
               << " samples=" << item.samples << '\n';
    }
    output << "[guest-throughput-evidence-end] schema="
           << kReportSchemaVersion << " tag=" << safe_tag
           << " thread-records=" << thread_attribution.size()
           << " pc-records=" << pc_report_count
           << " parent-records=" << parent_report_count << '\n';

    const InlineCheckpointScheduleSummary schedule =
        inline_checkpoint_schedule_summary();
    output << "[inline-checkpoint-schedule] schema=" << kReportSchemaVersion
           << " tag=" << safe_tag
           << " samples=" << schedule.samples
           << " unattributed=" << schedule.unattributed_samples
           << " gap-1=" << schedule.gap_buckets[0]
           << " gap-2-4=" << schedule.gap_buckets[1]
           << " gap-5-16=" << schedule.gap_buckets[2]
           << " gap-17-256=" << schedule.gap_buckets[3]
           << " gap-257-2047=" << schedule.gap_buckets[4]
           << " gap-2048-8192=" << schedule.gap_buckets[5]
           << " gap-over-8192=" << schedule.gap_buckets[6];
    for (std::size_t index = 0u;
         index < schedule.cause_samples.size();
         ++index) {
        const auto cause =
            static_cast<InlineCheckpointScheduleCause>(index);
        output << ' ' << inline_checkpoint_schedule_cause_name(cause)
               << '=' << schedule.cause_samples[index];
    }
    output << '\n';

    for (std::size_t index = 0; index < retained_vi_records(); ++index) {
        const ViRecord& record = retained_vi_record(index);
        output << "[frame-cadence-vi] tag=" << safe_tag
               << " boundary=" << record.boundary_serial
               << " deadline-sequence=" << record.deadline_sequence
               << " deadline-ticks=" << record.deadline_ticks
               << " actual-ticks=" << record.deadline_actual_ticks
               << " edge-ns=" << record.edge_consumed_ns
               << " capture-ns=" << record.capture_ns
               << " submit-begin-ns=" << record.submit_begin_ns
               << " submit-end-ns=" << record.submit_end_ns
               << " token-complete-ns=" << record.token_complete_ns
               << " vi-latched-ns=" << record.vi_latched_ns
               << " finalize-ns=" << record.finalize_ns
               << " guest-interval-ns=" << record.guest_interval_ns
               << " checkpoint-begin=" << record.checkpoint_begin
               << " checkpoint-end=" << record.checkpoint_end
               << " fifo-bytes=" << record.fifo_bytes
               << " copy-before=" << record.xfb_copy_serial_before
               << " copy-after=" << record.xfb_copy_serial_after
               << " selected-xfb=" << record.selected_xfb_addr
               << " jut-manager=" << record.jut_manager
               << " jut-buffer-count=" << record.jut_buffer_count
               << " jut-displaying-slot=" << record.jut_displaying_slot
               << " jut-slot0=" << record.jut_slot_addrs[0]
               << " jut-slot1=" << record.jut_slot_addrs[1]
               << " jut-slot2=" << record.jut_slot_addrs[2]
               << " token-kind=" << frame_token_kind_name(record.token_kind)
               << " token-epoch=" << record.token_epoch
               << " token-value=" << record.token_value
               << " token-completed=" << (record.token_completed ? 1 : 0)
               << " token-detached=" << (record.token_detached ? 1 : 0)
               << " token-wait-ns=" << record.token_wait_ns
               << " token-wait-slices=" << record.token_wait_slices
               << " rendered=" << (record.rendered_retrace ? 1 : 0)
               << '\n';
    }
    output << "[frame-cadence-evidence-end] schema="
           << kReportSchemaVersion << " tag=" << safe_tag
           << " records=" << retained_vi_records() << '\n';
}

std::uint64_t sample_now_ns_if_enabled(
    const Session* session,
    MonotonicReadFn reader,
    void* user) noexcept {
    if (session == nullptr || !session->enabled() || reader == nullptr) {
        return 0u;
    }
    return reader(user);
}

std::uint64_t steady_now_ns_if_enabled(const Session* session) noexcept {
    return sample_now_ns_if_enabled(session, &steady_clock_read, nullptr);
}

DspPollTimingSnapshot snapshot_dsp_poll_timing(
    const Session* session) noexcept {
    DspPollTimingSnapshot result{};
    if (session == nullptr || !session->enabled()) {
        return result;
    }
    for (std::size_t index = 0u; index < kDspPollTimingPhases.size(); ++index) {
        const auto phase = session->phase_snapshot(kDspPollTimingPhases[index]);
        result.counts[index] = phase.count;
        result.total_ns[index] = phase.total_ns;
    }
    result.valid = true;
    return result;
}

ScopedPhaseTimer::ScopedPhaseTimer(
    Session* session,
    TimingPhase phase) noexcept
    : session_(session),
      phase_(phase),
      start_ns_(steady_now_ns_if_enabled(session)) {}

ScopedPhaseTimer::~ScopedPhaseTimer() noexcept {
    if (session_ == nullptr || start_ns_ == 0u) {
        return;
    }
    const std::uint64_t end_ns = steady_now_ns_if_enabled(session_);
    session_->record_phase(
        phase_, end_ns >= start_ns_ ? end_ns - start_ns_ : 0u);
}

void configure_global_session(bool enabled) noexcept {
    g_active_session.store(nullptr, std::memory_order_release);
    g_global_session.reset();
    if (enabled) {
        g_active_session.store(&g_global_session, std::memory_order_release);
    }
}

Session* global_session_if_enabled() noexcept {
    return g_active_session.load(std::memory_order_acquire);
}

const char* timing_phase_name(TimingPhase phase) noexcept {
    switch (phase) {
    case TimingPhase::ProducerQueueWait:
        return "producer-queue-wait";
    case TimingPhase::LiveMemoryReadBarrierWait:
        return "live-memory-read-barrier-wait";
    case TimingPhase::RenderBegin:
        return "render-begin";
    case TimingPhase::RenderCacheInvalidation:
        return "render-cache-invalidation";
    case TimingPhase::RenderFifoParse:
        return "render-fifo-parse";
    case TimingPhase::RenderPostParseFlush:
        return "render-post-parse-flush";
    case TimingPhase::PeFenceWait:
        return "pe-fence-wait";
    case TimingPhase::PeConsumerWait:
        return "pe-consumer-wait";
    case TimingPhase::GuestCheckpointCallback:
        return "guest-checkpoint-callback";
    case TimingPhase::DspMramServiceWake:
        return "dsp-mram-service-wake";
    case TimingPhase::DspCheckpointPreflightPoll:
        return "dsp-checkpoint-preflight-poll";
    case TimingPhase::DspCheckpointPublicationPoll:
        return "dsp-checkpoint-publication-poll";
    case TimingPhase::DspCheckpointPreflightYield:
        return "dsp-checkpoint-preflight-yield";
    case TimingPhase::DspCheckpointPublicationYield:
        return "dsp-checkpoint-publication-yield";
    case TimingPhase::WorkerDependencyScan:
        return "worker-dependency-scan";
    case TimingPhase::WorkerSnapshotCopy:
        return "worker-snapshot-copy";
    case TimingPhase::RenderTextureInvalidation:
        return "render-texture-invalidation";
    case TimingPhase::RenderParserLockWait:
        return "render-parser-lock-wait";
    case TimingPhase::RenderDisplayListInvalidation:
        return "render-display-list-invalidation";
    case TimingPhase::RenderDecodedVertexInvalidation:
        return "render-decoded-vertex-invalidation";
    case TimingPhase::GuestTransferUnwind:
        return "guest-transfer-unwind";
    case TimingPhase::EfbPeekSynchronization:
        return "efb-peek-synchronization";
    case TimingPhase::EfbPeekReadback:
        return "efb-peek-readback";
    case TimingPhase::Count:
        break;
    }
    return "invalid";
}

const char* frame_token_kind_name(FrameTokenKind kind) noexcept {
    switch (kind) {
    case FrameTokenKind::None:
        return "none";
    case FrameTokenKind::EventFree:
        return "event-free";
    case FrameTokenKind::EventBearing:
        return "event-bearing";
    }
    return "invalid";
}

const char* inline_checkpoint_schedule_cause_name(
    InlineCheckpointScheduleCause cause) noexcept {
    switch (cause) {
    case InlineCheckpointScheduleCause::ForcedNext:
        return "forced-next";
    case InlineCheckpointScheduleCause::PeriodicViPoll:
        return "periodic-vi-poll";
    case InlineCheckpointScheduleCause::ViReadinessProbe:
        return "vi-readiness-probe";
    case InlineCheckpointScheduleCause::ViDeadline:
        return "vi-deadline";
    case InlineCheckpointScheduleCause::InputDeadline:
        return "input-deadline";
    case InlineCheckpointScheduleCause::RuntimeFailure:
        return "runtime-failure";
    case InlineCheckpointScheduleCause::DspDeadline:
        return "dsp-deadline";
    case InlineCheckpointScheduleCause::Decrementer:
        return "decrementer";
    case InlineCheckpointScheduleCause::AudioDeadline:
        return "audio-deadline";
    case InlineCheckpointScheduleCause::AudioFallback:
        return "audio-fallback";
    case InlineCheckpointScheduleCause::HostPump:
        return "host-pump";
    case InlineCheckpointScheduleCause::IpcPending:
        return "ipc-pending";
    case InlineCheckpointScheduleCause::DspInterfacePending:
        return "dsp-interface-pending";
    case InlineCheckpointScheduleCause::PePending:
        return "pe-pending";
    case InlineCheckpointScheduleCause::Count:
        break;
    }
    return "invalid";
}

}  // namespace galaxy::cadence
