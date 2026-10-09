#pragma once

#include "galaxy/dsp_alu.h"
#include "galaxy/native_audio_bus.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <mutex>
#include <span>

namespace galaxy {

// GALAXY_TRACE_DSP_HOST / GALAXY_TRACE_DSP_IFX diagnostics are printed to the
// shared std::cout/std::cerr streams from both the cooperative guest (main)
// thread (native_host.cpp's dsp_native_pump()) and the free-running native
// DSP worker thread (native_dsp.cpp's NativeDspWorker::thread_main(), plus
// the dsp_native_request_interrupt_cb() callback it invokes). Neither
// std::ostream nor its shared format-flag state (std::ios_base::fmtflags,
// set by std::hex/std::dec manipulators) is synchronized across threads, so
// concurrent trace prints from those two threads interleave and corrupt each
// other's hex/dec formatting mid-line (observed as decimal trace counters
// rendering as hex digits, and lines missing their terminating '\n' with a
// second thread's text spliced in). This mutex is a diagnostics-only
// correctness fix for that data race; it does not gate any guest-visible
// mailbox/interrupt state.
inline std::mutex g_dsp_trace_mutex;



// Thrown out of a free-running DSP entry when the host requests it to stop.
// The real ucode never returns on its own (its idle state is a tight
// mailbox-poll loop), so a cooperative abort is the only clean way to break
// the worker thread out of execution at shutdown. It is only ever thrown when
// DspContext::host_abort is wired and set, so single-threaded callers never
// see it.
struct DspExecutionAborted {};

// Thrown after a pending CPU->DSP external interrupt has been accepted. The
// helper that throws it has already pushed PC/SR and changed ctx.pc to the
// vector; the worker catches this to re-enter the compiled ucode at that vector.
struct DspExternalInterruptRequested {};

// Host-owned event used only to park a free-running native DSP while a known
// hardware-mailbox polling loop is genuinely idle. This covers the exact
// RMGE01 CMBH empty-mail and DMBH back-pressure back-edges classified below.
// The generation makes either wait immune to a notification arriving between
// the mailbox recheck and atomic::wait().
// DspContext keeps a pointer so reset() remains value-copyable.
struct DspHostWakeState {
    std::atomic<std::uint64_t> generation{};
    std::atomic<std::uint64_t> idle_wait_count{};
};

// Cumulative, native-AOT-only evidence counters. These are deliberately
// separate from the retired HLE mixer statistics: a native DSP run must be
// able to prove that generated instructions and the real IFX/DMA boundaries
// advanced without crediting a code path that was not executing.
//
// retired_instructions counts generated instruction-entry markers. The
// lowering emits one marker immediately before every instruction's semantics,
// so a hard trap can over-count by at most the one instruction that trapped.
// It is diagnostic liveness evidence, not a DSP cycle model and must never be
// used to pace the coprocessor.
struct DspNativeTelemetryState {
    // Even when quiescent; odd only while the DSP worker publishes the six
    // correlated instruction/back-edge counters below. Readers retry if they
    // overlap that short publication so a snapshot always names one prefix of
    // generated execution rather than a mixture of two prefixes.
    std::atomic<std::uint64_t> worker_publication_sequence{};
    std::atomic<std::uint64_t> retired_instructions{};
    std::atomic<std::uint64_t> zero_queue_idle_backedges{};
    std::atomic<std::uint64_t> command_wait_idle_backedges{};
    std::atomic<std::uint64_t> zero_queue_short_reentries{};
    std::atomic<std::uint64_t> zero_queue_complete_sequences{};
    std::atomic<std::uint64_t> command_wait_complete_sequences{};
    std::atomic<std::uint64_t> zero_queue_idle_waits{};
    std::atomic<std::uint64_t> command_wait_idle_waits{};
    std::atomic<std::uint64_t> cpu_mail_published{};
    std::atomic<std::uint64_t> cpu_mail_consumed{};
    std::atomic<std::uint64_t> dsp_mail_published{};
    std::atomic<std::uint64_t> dsp_mail_consumed{};
    std::atomic<std::uint64_t> dirq_writes{};
    std::atomic<std::uint64_t> dma_in_transfers{};
    std::atomic<std::uint64_t> dma_in_bytes{};
    std::atomic<std::uint64_t> dma_in_nonzero_bytes{};
    std::atomic<std::uint64_t> dma_out_transfers{};
    std::atomic<std::uint64_t> dma_out_bytes{};
    std::atomic<std::uint64_t> dma_out_nonzero_bytes{};
};

struct DspNativeTelemetrySnapshot {
    std::uint64_t retired_instructions{};
    // Exact taken-edge counts for the two statically certified RMGE01 idle
    // SCCs. They are evidence counters, not DSP clocks or pacing inputs.
    std::uint64_t zero_queue_idle_backedges{};
    std::uint64_t command_wait_idle_backedges{};
    // Counts the 0x0824 -> 0x07FA alternate entry, which reaches the next
    // 0x0820 edge through five markers instead of the steady-state eight.
    std::uint64_t zero_queue_short_reentries{};
    // Unlike the raw edge counters above, these require every marker in the
    // statically audited SCC, in order, immediately before the taken edge.
    std::uint64_t zero_queue_complete_sequences{};
    std::uint64_t command_wait_complete_sequences{};
    std::uint64_t zero_queue_idle_waits{};
    std::uint64_t command_wait_idle_waits{};
    std::uint64_t cpu_mail_published{};
    std::uint64_t cpu_mail_consumed{};
    std::uint64_t dsp_mail_published{};
    std::uint64_t dsp_mail_consumed{};
    std::uint64_t dirq_writes{};
    std::uint64_t dma_in_transfers{};
    std::uint64_t dma_in_bytes{};
    std::uint64_t dma_in_nonzero_bytes{};
    std::uint64_t dma_out_transfers{};
    std::uint64_t dma_out_bytes{};
    std::uint64_t dma_out_nonzero_bytes{};
    // Host scheduling evidence only: number of event waits entered at an
    // exactly classified mailbox back-edge. This is not a DSP cycle count.
    std::uint64_t host_idle_waits{};
    // Only the six worker instruction/idle counters share a publication. When
    // false, individual counters remain acquired observations, but a caller
    // must not infer their correlated interval or instruction liveness.
    bool worker_counters_coherent = true;
};

// Opt-in, worker-owned broad evidence for the native JAudio DSP pipeline.
// The generated DSP thread is the only writer while it is running, and the
// owner copies a snapshot only after the worker has stopped.  This recorder is
// intentionally perturbative: when enabled it observes high-frequency mixer
// writes, DMA traffic, and accelerator reads.  It must not be used as strict
// cadence or performance proof.
//
// DMA classification is deliberately exact and RMGE01-specific.  These are
// the transfer shapes used by the verified JAudio ucode: one 0x180-byte voice
// record enters D[0x0800], software-decoded sample windows enter D[0x0B00] or
// D[0x0B60], and the six SEND_TABLE mix buses leave byte addresses 0x1A00,
// 0x1AC0, 0x1B80, 0x1C40, 0x1D00, and 0x1DC0 as planar 0xA0-byte blocks.
// Unknown shapes remain generic DMA evidence; they never receive credit for a
// later causal stage.
enum class DspAudioCausalEventKind : std::uint8_t {
    DmaIn,
    DmaOut,
    ChannelRecordIn,
    SampleWindowIn,
    OutputBlockOut,
    MixerOutputWrite,
    AcceleratorStopped,
    AcceleratorActive,
    AcceleratorRawNonzero,
    AcceleratorDecodedNonzero,
};

struct DspAudioCausalEvent {
    std::uint64_t sequence{};
    DspAudioCausalEventKind kind{DspAudioCausalEventKind::DmaIn};
    std::uint16_t pc{};
    std::uint16_t control_or_format{};
    std::uint32_t host_address{};
    std::uint32_t dsp_byte_address{};
    std::uint32_t length{};
    std::uint32_t nonzero_bytes{};
    std::uint32_t accelerator_address{};
    std::int16_t raw_sample{};
    std::uint16_t decoded_sample{};
    std::uint8_t mixer_first_index{0xFFu};
    std::uint8_t mixer_nonzero_tuples{};
    std::uint8_t mixer_nonzero_gain_tuples{};
    std::uint16_t mixer_bus{};
    std::uint16_t mixer_current{};
    std::uint16_t mixer_target{};
    std::uint16_t mixer_delta{};

    bool operator==(const DspAudioCausalEvent&) const = default;
};

inline constexpr std::size_t kDspAudioCausalRecordCapacity = 64u;
static_assert(kDspAudioCausalRecordCapacity != 0u);

struct DspAudioCausalRecorder {
    std::array<DspAudioCausalEvent, kDspAudioCausalRecordCapacity> records{};
    std::size_t next_record{};
    std::size_t record_count{};
    std::uint64_t total_events{};
    std::uint64_t overwritten_events{};

    std::uint64_t dma_in_transfers{};
    std::uint64_t dma_in_bytes{};
    std::uint64_t dma_in_nonzero_bytes{};
    std::uint64_t dma_out_transfers{};
    std::uint64_t dma_out_bytes{};
    std::uint64_t dma_out_nonzero_bytes{};
    std::uint64_t channel_record_transfers{};
    std::uint64_t channel_record_nonzero_transfers{};
    std::uint64_t channel_record_nonzero_bytes{};
    std::uint64_t channel_mixer_inspected_transfers{};
    std::uint64_t channel_mixer_nonzero_record_transfers{};
    std::uint64_t channel_mixer_nonzero_tuples{};
    std::uint64_t channel_mixer_nonzero_gain_tuples{};
    // The RMGE01 ucode's external-interrupt handler writes the four 16-bit
    // channel-selection bitmap words to D[0x04FC..0x04FF] at PC 0x0715.
    // The render loop tests those exact words before it can issue the
    // D[0x0800], 0x180-byte channel-record DMA.  Capturing the writes here
    // distinguishes an explicitly idle guest publication from a lost/broken
    // channel-record handoff without inspecting or synthesizing guest state.
    std::uint64_t channel_selection_writes{};
    std::uint64_t channel_selection_nonzero_writes{};
    std::uint64_t channel_selection_nonzero_bits{};
    std::uint8_t channel_selection_valid_mask{};
    std::array<std::uint16_t, 4> channel_selection_words{};
    std::uint64_t sample_window_transfers{};
    std::uint64_t sample_window_nonzero_transfers{};
    std::uint64_t sample_window_nonzero_bytes{};
    std::uint64_t mixer_output_word_writes{};
    std::uint64_t mixer_output_nonzero_word_writes{};
    std::uint64_t primary_mix_bus_word_writes{};
    std::uint64_t primary_mix_bus_nonzero_word_writes{};
    std::uint64_t output_block_transfers{};
    std::uint64_t output_block_nonzero_transfers{};
    std::uint64_t output_block_nonzero_bytes{};
    std::uint64_t accelerator_calls{};
    std::uint64_t accelerator_stopped_calls{};
    std::uint64_t accelerator_active_reads{};
    std::uint64_t accelerator_raw_nonzero_reads{};
    std::uint64_t accelerator_decoded_nonzero_reads{};

    bool has_last_dma_in{};
    bool has_last_nonzero_dma_in{};
    bool has_last_dma_out{};
    bool has_last_channel_record{};
    bool has_first_routed_channel_record{};
    bool has_last_nonzero_channel_record{};
    bool has_last_sample_window{};
    bool has_last_output_block{};
    bool has_last_accelerator_sample{};
    DspAudioCausalEvent last_dma_in{};
    DspAudioCausalEvent last_nonzero_dma_in{};
    DspAudioCausalEvent last_dma_out{};
    DspAudioCausalEvent last_channel_record{};
    DspAudioCausalEvent first_routed_channel_record{};
    DspAudioCausalEvent last_nonzero_channel_record{};
    DspAudioCausalEvent last_sample_window{};
    DspAudioCausalEvent last_output_block{};
    DspAudioCausalEvent last_accelerator_sample{};
};

struct DspAudioCausalSnapshot {
    // Chronological retained suffix.  Once capacity is reached the oldest
    // record is overwritten, while aggregate counters and exact last-stage
    // records below remain lossless.
    std::array<DspAudioCausalEvent, kDspAudioCausalRecordCapacity> records{};
    std::size_t record_count{};
    std::uint64_t total_events{};
    std::uint64_t overwritten_events{};

    std::uint64_t dma_in_transfers{};
    std::uint64_t dma_in_bytes{};
    std::uint64_t dma_in_nonzero_bytes{};
    std::uint64_t dma_out_transfers{};
    std::uint64_t dma_out_bytes{};
    std::uint64_t dma_out_nonzero_bytes{};
    std::uint64_t channel_record_transfers{};
    std::uint64_t channel_record_nonzero_transfers{};
    std::uint64_t channel_record_nonzero_bytes{};
    std::uint64_t channel_mixer_inspected_transfers{};
    std::uint64_t channel_mixer_nonzero_record_transfers{};
    std::uint64_t channel_mixer_nonzero_tuples{};
    std::uint64_t channel_mixer_nonzero_gain_tuples{};
    std::uint64_t channel_selection_writes{};
    std::uint64_t channel_selection_nonzero_writes{};
    std::uint64_t channel_selection_nonzero_bits{};
    std::uint8_t channel_selection_valid_mask{};
    std::array<std::uint16_t, 4> channel_selection_words{};
    std::uint64_t sample_window_transfers{};
    std::uint64_t sample_window_nonzero_transfers{};
    std::uint64_t sample_window_nonzero_bytes{};
    std::uint64_t mixer_output_word_writes{};
    std::uint64_t mixer_output_nonzero_word_writes{};
    std::uint64_t primary_mix_bus_word_writes{};
    std::uint64_t primary_mix_bus_nonzero_word_writes{};
    std::uint64_t output_block_transfers{};
    std::uint64_t output_block_nonzero_transfers{};
    std::uint64_t output_block_nonzero_bytes{};
    std::uint64_t accelerator_calls{};
    std::uint64_t accelerator_stopped_calls{};
    std::uint64_t accelerator_active_reads{};
    std::uint64_t accelerator_raw_nonzero_reads{};
    std::uint64_t accelerator_decoded_nonzero_reads{};

    bool has_last_dma_in{};
    bool has_last_nonzero_dma_in{};
    bool has_last_dma_out{};
    bool has_last_channel_record{};
    bool has_first_routed_channel_record{};
    bool has_last_nonzero_channel_record{};
    bool has_last_sample_window{};
    bool has_last_output_block{};
    bool has_last_accelerator_sample{};
    DspAudioCausalEvent last_dma_in{};
    DspAudioCausalEvent last_nonzero_dma_in{};
    DspAudioCausalEvent last_dma_out{};
    DspAudioCausalEvent last_channel_record{};
    DspAudioCausalEvent first_routed_channel_record{};
    DspAudioCausalEvent last_nonzero_channel_record{};
    DspAudioCausalEvent last_sample_window{};
    DspAudioCausalEvent last_output_block{};
    DspAudioCausalEvent last_accelerator_sample{};
    bool quiescent_mixer_level_valid{};
    std::uint16_t quiescent_mixer_level{};
};

// Generated DSP diagnostics are an explicit source/runtime contract.  The
// generator emits literal version/capability exports; the native host requires
// both hooks before it starts RMGE01 ucode.  A stale generated translation then
// fails at link time (missing exports) or boot time (version/mask mismatch)
// instead of producing a false "no publication" diagnostic.
inline constexpr std::uint32_t kDspGeneratedProbeContractVersion = 1u;
inline constexpr std::uint32_t
    kDspGeneratedProbeCapabilitySelectionPublication = 1u << 0u;
inline constexpr std::uint32_t
    kDspGeneratedProbeCapabilitySelectedChannelBranch = 1u << 1u;
inline constexpr std::uint32_t kDspGeneratedProbeRequiredCapabilities =
    kDspGeneratedProbeCapabilitySelectionPublication |
    kDspGeneratedProbeCapabilitySelectedChannelBranch;

[[nodiscard]] inline constexpr bool dsp_generated_probe_contract_compatible(
    std::uint32_t version,
    std::uint32_t capabilities) noexcept {
    return version == kDspGeneratedProbeContractVersion &&
           (capabilities & kDspGeneratedProbeRequiredCapabilities) ==
               kDspGeneratedProbeRequiredCapabilities;
}

// A separate, rare-event diagnostic for one exact RMGE01 handoff.  The
// selection side is called only from statically lowered PC 0x0715.  A second
// static hook at the selected-channel render call (PC 0x02EF -> 0x00CC)
// captures the channel index, the exact selection word/bit that admitted it,
// and the channel-record MRAM address calculated in D[0x034C:0x034D].  Only a
// subsequent successful PC 0x05EF host-to-D[0x0800], length 0x180 DMA with the
// same address can receive selected-channel causal credit.  An arbitrary DMA
// with the same shape is lifetime traffic, never proof of this handoff.
//
// Four selection words constitute a publication only when the handler writes
// D[0x04FC], D[0x04FD], D[0x04FE], and D[0x04FF] consecutively in that exact
// order.  The first later execution of that exact handler store (including an
// out-of-range address) closes the causal window immediately; no DMA can be
// credited until another full ordered publication completes.  Both hooks and
// DMA observation execute on the one
// generated DSP worker.  No live snapshot is permitted; the host copies state
// only after join and carries the lifetime counters across DSPCR resets.
struct DspChannelSelectionDmaProbeRecorder {
    std::uint32_t generated_probe_contract_version{};
    std::uint32_t generated_probe_capabilities{};
    std::uint64_t worker_sequence{};
    std::uint64_t publication_generation{};
    std::uint64_t session_count{};
    std::uint64_t reset_count{};
    std::uint64_t handler_writes{};
    std::uint64_t in_range_handler_writes{};
    std::uint64_t out_of_range_handler_writes{};
    std::uint64_t invalid_order_writes{};
    std::uint64_t intervening_selection_writes{};
    std::uint64_t complete_publications{};
    std::uint64_t active_complete_publications{};
    std::uint64_t zero_complete_publications{};
    std::uint64_t expired_unmatched_active_publications{};
    std::uint64_t selected_channel_branch_entries{};
    std::uint64_t validated_selected_channel_branches{};
    std::uint64_t rejected_selected_channel_branches{};
    std::uint64_t branches_after_intervening_selection{};
    std::uint64_t expired_pending_selected_channel_branches{};
    std::uint64_t skipped_dma_attempts_with_pending_branch{};
    std::uint64_t exact_channel_record_dmas{};
    std::uint64_t validated_selected_channel_record_dmas{};
    std::uint64_t unmatched_channel_record_dmas{};
    std::uint64_t dmas_without_validated_selected_channel_branch{};
    std::uint64_t selected_channel_dma_identity_mismatches{};
    std::uint64_t dmas_after_intervening_selection{};
    std::uint64_t matched_active_publications{};
    std::uint64_t current_publication_generation{};
    std::uint64_t current_publication_session{};
    std::uint64_t current_publication_sequence{};
    std::uint64_t current_first_match_sequence{};
    std::uint64_t last_exact_dma_sequence{};
    std::uint64_t last_matched_publication_generation{};
    std::uint64_t last_matched_publication_session{};
    std::uint64_t validated_channel_sequence{};
    std::uint32_t last_exact_dma_host_address{};
    std::uint32_t current_first_match_dma_host_address{};
    std::uint16_t last_exact_dma_pc{};
    std::uint16_t current_first_match_dma_pc{};
    std::uint16_t current_first_match_channel{};
    std::uint16_t last_matched_channel{};
    std::uint16_t last_validated_channel{};
    std::uint8_t partial_word_count{};
    std::array<std::uint16_t, 4> partial_words{};
    std::array<std::uint16_t, 4> current_words{};
    std::uint64_t pending_branch_publication_generation{};
    std::uint64_t pending_branch_session{};
    std::uint64_t pending_branch_sequence{};
    std::uint32_t pending_branch_expected_host_address{};
    std::uint16_t pending_branch_channel{};
    std::uint16_t pending_branch_selection_word{};
    bool current_publication_valid{};
    bool current_publication_active{};
    bool current_publication_matched{};
    bool current_publication_blocked_by_selection_write{};
    bool pending_selected_channel_branch_valid{};
};

using DspChannelSelectionDmaProbeSnapshot =
    DspChannelSelectionDmaProbeRecorder;

enum class DspChannelSelectionDmaProbeState : std::uint8_t {
    NoCompletePublication,
    IncompletePublicationAfterCurrent,
    CurrentPublicationZero,
    ActiveAwaitingValidatedSelectedChannelDma,
    ActiveWithValidatedSelectedChannelDma,
};

enum class DspChannelSelectionDmaProbeLifetimeState : std::uint8_t {
    NoCompletePublicationEver,
    ZeroPublicationsOnly,
    ActivePublicationWithoutValidatedSelectedChannelDma,
    ActivePublicationWithValidatedSelectedChannelDma,
};

[[nodiscard]] inline constexpr DspChannelSelectionDmaProbeState
dsp_channel_selection_dma_probe_state(
    const DspChannelSelectionDmaProbeSnapshot& snapshot) noexcept {
    if (!snapshot.current_publication_valid) {
        return DspChannelSelectionDmaProbeState::NoCompletePublication;
    }
    if (snapshot.current_publication_blocked_by_selection_write) {
        return DspChannelSelectionDmaProbeState::
            IncompletePublicationAfterCurrent;
    }
    if (!snapshot.current_publication_active) {
        return DspChannelSelectionDmaProbeState::CurrentPublicationZero;
    }
    return snapshot.current_publication_matched
        ? DspChannelSelectionDmaProbeState::
              ActiveWithValidatedSelectedChannelDma
        : DspChannelSelectionDmaProbeState::
              ActiveAwaitingValidatedSelectedChannelDma;
}

[[nodiscard]] inline constexpr const char*
dsp_channel_selection_dma_probe_state_name(
    DspChannelSelectionDmaProbeState state) noexcept {
    switch (state) {
    case DspChannelSelectionDmaProbeState::NoCompletePublication:
        return "no-complete-publication";
    case DspChannelSelectionDmaProbeState::IncompletePublicationAfterCurrent:
        return "incomplete-publication-after-current";
    case DspChannelSelectionDmaProbeState::CurrentPublicationZero:
        return "current-publication-zero";
    case DspChannelSelectionDmaProbeState::
        ActiveAwaitingValidatedSelectedChannelDma:
        return "active-awaiting-validated-selected-channel-dma";
    case DspChannelSelectionDmaProbeState::
        ActiveWithValidatedSelectedChannelDma:
        return "active-with-validated-selected-channel-dma";
    }
    return "invalid";
}

[[nodiscard]] inline constexpr DspChannelSelectionDmaProbeLifetimeState
dsp_channel_selection_dma_probe_lifetime_state(
    const DspChannelSelectionDmaProbeSnapshot& snapshot) noexcept {
    if (snapshot.complete_publications == 0u) {
        return DspChannelSelectionDmaProbeLifetimeState::
            NoCompletePublicationEver;
    }
    if (snapshot.active_complete_publications == 0u) {
        return DspChannelSelectionDmaProbeLifetimeState::ZeroPublicationsOnly;
    }
    return snapshot.matched_active_publications == 0u
        ? DspChannelSelectionDmaProbeLifetimeState::
              ActivePublicationWithoutValidatedSelectedChannelDma
        : DspChannelSelectionDmaProbeLifetimeState::
              ActivePublicationWithValidatedSelectedChannelDma;
}

[[nodiscard]] inline constexpr const char*
dsp_channel_selection_dma_probe_lifetime_state_name(
    DspChannelSelectionDmaProbeLifetimeState state) noexcept {
    switch (state) {
    case DspChannelSelectionDmaProbeLifetimeState::NoCompletePublicationEver:
        return "no-complete-publication-ever";
    case DspChannelSelectionDmaProbeLifetimeState::ZeroPublicationsOnly:
        return "zero-publications-only";
    case DspChannelSelectionDmaProbeLifetimeState::
        ActivePublicationWithoutValidatedSelectedChannelDma:
        return "active-publication-without-validated-selected-channel-dma";
    case DspChannelSelectionDmaProbeLifetimeState::
        ActivePublicationWithValidatedSelectedChannelDma:
        return "active-publication-with-validated-selected-channel-dma";
    }
    return "invalid";
}

enum class DspAudioCausalClassification : std::uint8_t {
    NoDmaInput,
    DmaInputSilent,
    NoChannelRecord,
    ChannelRecordSilent,
    ChannelRoutingUnobserved,
    ChannelRoutingSilent,
    NoSampleSource,
    SampleSourceSilent,
    MixerOutputUnwritten,
    MixerOutputSilent,
    OutputDmaAbsent,
    OutputDmaSilent,
    OutputObserved,
};

enum class DspAudioCausalChannelSelectionState : std::uint8_t {
    Unpublished,
    PublishedZeroOnly,
    ActivePublishedWithoutChannelRecord,
    ActivePublishedWithChannelRecord,
};

[[nodiscard]] inline constexpr DspAudioCausalChannelSelectionState
dsp_audio_causal_channel_selection_state(
    const DspAudioCausalSnapshot& snapshot) noexcept {
    // This is a lifetime aggregate retained for the broad diagnostic report;
    // it does not prove that a channel DMA followed a particular selection
    // publication.  Use DspChannelSelectionDmaProbeSnapshot for ordering.
    if (snapshot.channel_selection_writes == 0u) {
        return DspAudioCausalChannelSelectionState::Unpublished;
    }
    if (snapshot.channel_selection_nonzero_writes == 0u) {
        return DspAudioCausalChannelSelectionState::PublishedZeroOnly;
    }
    if (snapshot.channel_record_transfers == 0u) {
        return DspAudioCausalChannelSelectionState::
            ActivePublishedWithoutChannelRecord;
    }
    return DspAudioCausalChannelSelectionState::ActivePublishedWithChannelRecord;
}

[[nodiscard]] inline constexpr const char*
dsp_audio_causal_channel_selection_state_name(
    DspAudioCausalChannelSelectionState state) noexcept {
    switch (state) {
    case DspAudioCausalChannelSelectionState::Unpublished:
        return "unpublished";
    case DspAudioCausalChannelSelectionState::PublishedZeroOnly:
        return "published-zero-only";
    case DspAudioCausalChannelSelectionState::
        ActivePublishedWithoutChannelRecord:
        return "active-published-record-missing";
    case DspAudioCausalChannelSelectionState::ActivePublishedWithChannelRecord:
        return "active-published-record-seen";
    }
    return "invalid";
}

[[nodiscard]] inline constexpr DspAudioCausalEventKind
dsp_audio_causal_classify_dma(
    bool to_host,
    bool instruction_memory,
    std::uint32_t dsp_byte_address,
    std::uint32_t length) noexcept {
    if (instruction_memory) {
        return to_host ? DspAudioCausalEventKind::DmaOut
                       : DspAudioCausalEventKind::DmaIn;
    }
    if (!to_host && dsp_byte_address == 0x0800u && length == 0x0180u) {
        return DspAudioCausalEventKind::ChannelRecordIn;
    }
    if (!to_host &&
        ((dsp_byte_address == 0x0B00u && length == 0x00A0u) ||
         (dsp_byte_address == 0x0B60u && length == 0x00C0u))) {
        return DspAudioCausalEventKind::SampleWindowIn;
    }
    if (to_host && length == 0x00A0u &&
        (dsp_byte_address == 0x1A00u ||
         dsp_byte_address == 0x1AC0u ||
         dsp_byte_address == 0x1B80u ||
         dsp_byte_address == 0x1C40u ||
         dsp_byte_address == 0x1D00u ||
         dsp_byte_address == 0x1DC0u)) {
        return DspAudioCausalEventKind::OutputBlockOut;
    }
    return to_host ? DspAudioCausalEventKind::DmaOut
                   : DspAudioCausalEventKind::DmaIn;
}

inline void dsp_audio_causal_add(
    std::uint64_t& counter,
    std::uint64_t amount = 1u) {
    if (amount > std::numeric_limits<std::uint64_t>::max() - counter) {
        std::abort();
    }
    counter += amount;
}

inline void dsp_channel_selection_dma_probe_reset_partial(
    DspChannelSelectionDmaProbeRecorder& recorder) noexcept {
    recorder.partial_word_count = 0u;
    recorder.partial_words = {};
}

inline void dsp_channel_selection_dma_probe_clear_pending_branch(
    DspChannelSelectionDmaProbeRecorder& recorder) noexcept {
    recorder.pending_selected_channel_branch_valid = false;
    recorder.pending_branch_publication_generation = 0u;
    recorder.pending_branch_session = 0u;
    recorder.pending_branch_sequence = 0u;
    recorder.pending_branch_expected_host_address = 0u;
    recorder.pending_branch_channel = 0u;
    recorder.pending_branch_selection_word = 0u;
}

// Start a new worker session without erasing lifetime evidence.  This is used
// both by a coprocessor reset and when a quiescent snapshot is restored after a
// DSPCR teardown.  Current/partial state cannot cross the hardware reset.
inline void dsp_channel_selection_dma_probe_begin_session(
    DspChannelSelectionDmaProbeRecorder& recorder) {
    if (recorder.current_publication_valid &&
        recorder.current_publication_active &&
        !recorder.current_publication_matched) {
        dsp_audio_causal_add(
            recorder.expired_unmatched_active_publications);
    }
    if (recorder.pending_selected_channel_branch_valid) {
        dsp_audio_causal_add(
            recorder.expired_pending_selected_channel_branches);
    }
    dsp_audio_causal_add(recorder.session_count);
    dsp_channel_selection_dma_probe_reset_partial(recorder);
    dsp_channel_selection_dma_probe_clear_pending_branch(recorder);
    recorder.current_publication_generation = 0u;
    recorder.current_publication_session = 0u;
    recorder.current_publication_sequence = 0u;
    recorder.current_first_match_sequence = 0u;
    recorder.current_first_match_dma_host_address = 0u;
    recorder.current_first_match_dma_pc = 0u;
    recorder.current_first_match_channel = 0u;
    recorder.validated_channel_sequence = 0u;
    recorder.last_validated_channel = 0u;
    recorder.current_words = {};
    recorder.current_publication_valid = false;
    recorder.current_publication_active = false;
    recorder.current_publication_matched = false;
    recorder.current_publication_blocked_by_selection_write = false;
}

// DSPCR reset is an ordered lifetime event.  It closes every current causal
// window but deliberately retains totals, generations, and prior matches so a
// later process-exit report cannot lose evidence from the previous DSP task.
inline void dsp_channel_selection_dma_probe_record_reset_boundary(
    DspChannelSelectionDmaProbeRecorder& recorder) {
    dsp_audio_causal_add(recorder.worker_sequence);
    dsp_audio_causal_add(recorder.reset_count);
    if (recorder.current_publication_valid &&
        recorder.current_publication_active &&
        !recorder.current_publication_matched) {
        dsp_audio_causal_add(
            recorder.expired_unmatched_active_publications);
    }
    if (recorder.pending_selected_channel_branch_valid) {
        dsp_audio_causal_add(
            recorder.expired_pending_selected_channel_branches);
    }
    dsp_channel_selection_dma_probe_reset_partial(recorder);
    dsp_channel_selection_dma_probe_clear_pending_branch(recorder);
    recorder.current_publication_generation = 0u;
    recorder.current_publication_session = 0u;
    recorder.current_publication_sequence = 0u;
    recorder.current_first_match_sequence = 0u;
    recorder.current_first_match_dma_host_address = 0u;
    recorder.current_first_match_dma_pc = 0u;
    recorder.current_first_match_channel = 0u;
    recorder.validated_channel_sequence = 0u;
    recorder.last_validated_channel = 0u;
    recorder.current_words = {};
    recorder.current_publication_valid = false;
    recorder.current_publication_active = false;
    recorder.current_publication_matched = false;
    recorder.current_publication_blocked_by_selection_write = false;
}

// Emitted only for the exact RMGE01 `srr @$AR0,$AC0.M` at DSP PC 0x0715.
// Keeping this out of dsp_dram_write avoids a branch on every ordinary mixer
// store while the diagnostic is disabled or enabled.
inline void dsp_channel_selection_dma_probe_record_selection_write(
    DspChannelSelectionDmaProbeRecorder* recorder,
    std::uint16_t address,
    std::uint16_t value) {
    if (recorder == nullptr) {
        return;
    }

    dsp_audio_causal_add(recorder->worker_sequence);
    dsp_audio_causal_add(recorder->handler_writes);
    // Every execution of the exact handler store is an intervening selection
    // attempt, even if a corrupted AR0 points outside the four-word bitmap.
    // Fail closed before classifying the address so neither a current
    // publication nor a pending branch identity can survive that attempt.
    if (recorder->current_publication_valid) {
        dsp_audio_causal_add(recorder->intervening_selection_writes);
        recorder->current_publication_blocked_by_selection_write = true;
    }
    if (recorder->pending_selected_channel_branch_valid) {
        dsp_audio_causal_add(
            recorder->expired_pending_selected_channel_branches);
        dsp_channel_selection_dma_probe_clear_pending_branch(*recorder);
    }
    if (address < 0x04FCu || address > 0x04FFu) {
        dsp_audio_causal_add(recorder->out_of_range_handler_writes);
        if (recorder->partial_word_count != 0u) {
            dsp_audio_causal_add(recorder->invalid_order_writes);
            dsp_channel_selection_dma_probe_reset_partial(*recorder);
        }
        return;
    }

    dsp_audio_causal_add(recorder->in_range_handler_writes);
    const auto index = static_cast<std::uint8_t>(address - 0x04FCu);
    if (index != recorder->partial_word_count) {
        dsp_audio_causal_add(recorder->invalid_order_writes);
        dsp_channel_selection_dma_probe_reset_partial(*recorder);
        // A new 0x04FC write starts a fresh candidate immediately; every other
        // out-of-order word remains incomplete until the next 0x04FC.
        if (index != 0u) {
            return;
        }
    }

    recorder->partial_words[index] = value;
    ++recorder->partial_word_count;
    if (recorder->partial_word_count != 4u) {
        return;
    }

    if (recorder->current_publication_valid &&
        recorder->current_publication_active &&
        !recorder->current_publication_matched) {
        dsp_audio_causal_add(
            recorder->expired_unmatched_active_publications);
    }
    dsp_audio_causal_add(recorder->publication_generation);
    dsp_audio_causal_add(recorder->complete_publications);
    recorder->current_publication_generation =
        recorder->publication_generation;
    recorder->current_publication_session = recorder->session_count;
    recorder->current_publication_sequence = recorder->worker_sequence;
    recorder->current_first_match_sequence = 0u;
    recorder->current_first_match_dma_host_address = 0u;
    recorder->current_first_match_dma_pc = 0u;
    recorder->current_first_match_channel = 0u;
    recorder->current_words = recorder->partial_words;
    recorder->current_publication_valid = true;
    recorder->current_publication_active = std::any_of(
        recorder->current_words.begin(),
        recorder->current_words.end(),
        [](std::uint16_t word) { return word != 0u; });
    recorder->current_publication_matched = false;
    recorder->current_publication_blocked_by_selection_write = false;
    if (recorder->current_publication_active) {
        dsp_audio_causal_add(recorder->active_complete_publications);
    } else {
        dsp_audio_causal_add(recorder->zero_complete_publications);
    }
    dsp_channel_selection_dma_probe_reset_partial(*recorder);
}

// Emitted only for RMGE01 PC 0x02EF after the mask branch admitted one
// channel, and before its direct call to the 0x00CC channel-record DMA helper.
inline void dsp_channel_selection_dma_probe_record_selected_channel_branch(
    DspChannelSelectionDmaProbeRecorder* recorder,
    std::uint16_t channel,
    std::uint16_t observed_selection_word,
    std::uint32_t expected_host_address) {
    if (recorder == nullptr) {
        return;
    }

    dsp_audio_causal_add(recorder->worker_sequence);
    dsp_audio_causal_add(recorder->selected_channel_branch_entries);
    if (recorder->pending_selected_channel_branch_valid) {
        dsp_audio_causal_add(
            recorder->expired_pending_selected_channel_branches);
        dsp_channel_selection_dma_probe_clear_pending_branch(*recorder);
    }

    const bool ordered_publication =
        recorder->current_publication_valid &&
        recorder->current_publication_active &&
        !recorder->current_publication_blocked_by_selection_write &&
        recorder->worker_sequence > recorder->current_publication_sequence;
    if (!ordered_publication) {
        dsp_audio_causal_add(recorder->rejected_selected_channel_branches);
        if (recorder->current_publication_blocked_by_selection_write) {
            dsp_audio_causal_add(
                recorder->branches_after_intervening_selection);
        }
        return;
    }

    constexpr std::uint16_t kChannelCount = 64u;
    if (channel >= kChannelCount) {
        dsp_audio_causal_add(recorder->rejected_selected_channel_branches);
        return;
    }
    const auto word_index = static_cast<std::size_t>(channel >> 4u);
    const auto selected_bit = static_cast<std::uint16_t>(
        0x8000u >> (channel & 0x000Fu));
    const std::uint16_t published_selection_word =
        recorder->current_words[word_index];
    if (observed_selection_word != published_selection_word ||
        (published_selection_word & selected_bit) == 0u) {
        dsp_audio_causal_add(recorder->rejected_selected_channel_branches);
        return;
    }

    dsp_audio_causal_add(recorder->validated_selected_channel_branches);
    recorder->pending_selected_channel_branch_valid = true;
    recorder->pending_branch_publication_generation =
        recorder->current_publication_generation;
    recorder->pending_branch_session = recorder->current_publication_session;
    recorder->pending_branch_sequence = recorder->worker_sequence;
    recorder->pending_branch_expected_host_address = expected_host_address;
    recorder->pending_branch_channel = channel;
    recorder->pending_branch_selection_word = observed_selection_word;
}

// A DSBL write is a one-shot DMA attempt.  Hardware-disabled (AMDM) and
// zero-length attempts do not transfer bytes, but DSBL is still consumed by the
// IFX path.  Consequently a branch identity cannot remain pending for a later
// unrelated transfer after either skip condition.
inline void dsp_channel_selection_dma_probe_record_skipped_dma_attempt(
    DspChannelSelectionDmaProbeRecorder* recorder) {
    if (recorder == nullptr ||
        !recorder->pending_selected_channel_branch_valid) {
        return;
    }
    dsp_audio_causal_add(recorder->worker_sequence);
    dsp_audio_causal_add(
        recorder->skipped_dma_attempts_with_pending_branch);
    dsp_audio_causal_add(recorder->expired_pending_selected_channel_branches);
    dsp_channel_selection_dma_probe_clear_pending_branch(*recorder);
}

inline void dsp_channel_selection_dma_probe_record_dma(
    DspChannelSelectionDmaProbeRecorder* recorder,
    std::uint16_t pc,
    bool instruction_memory,
    bool to_host,
    std::uint32_t host_address,
    std::uint32_t dsp_byte_address,
    std::uint32_t length) {
    if (recorder == nullptr) {
        return;
    }

    const bool exact_channel_record_shape =
        !instruction_memory && !to_host && dsp_byte_address == 0x0800u &&
        length == 0x0180u;
    if (!exact_channel_record_shape) {
        // Once the exact render-branch hook has opened its one-DMA window, any
        // other completed DMA proves that the expected direct call was not the
        // next DMA.  Consume the identity instead of letting it match later.
        if (recorder->pending_selected_channel_branch_valid) {
            dsp_audio_causal_add(recorder->worker_sequence);
            dsp_audio_causal_add(
                recorder->expired_pending_selected_channel_branches);
            dsp_channel_selection_dma_probe_clear_pending_branch(*recorder);
        }
        return;
    }

    dsp_audio_causal_add(recorder->worker_sequence);
    dsp_audio_causal_add(recorder->exact_channel_record_dmas);
    recorder->last_exact_dma_sequence = recorder->worker_sequence;
    recorder->last_exact_dma_host_address = host_address;
    recorder->last_exact_dma_pc = pc;
    const bool had_validated_branch =
        recorder->pending_selected_channel_branch_valid;
    const std::uint64_t branch_generation =
        recorder->pending_branch_publication_generation;
    const std::uint64_t branch_session = recorder->pending_branch_session;
    const std::uint64_t branch_sequence = recorder->pending_branch_sequence;
    const std::uint32_t branch_host_address =
        recorder->pending_branch_expected_host_address;
    const std::uint16_t branch_channel = recorder->pending_branch_channel;
    dsp_channel_selection_dma_probe_clear_pending_branch(*recorder);

    const bool current_order_valid =
        recorder->current_publication_valid &&
        recorder->current_publication_active &&
        !recorder->current_publication_blocked_by_selection_write &&
        recorder->worker_sequence > recorder->current_publication_sequence;
    const bool branch_identity_valid =
        had_validated_branch && pc == 0x05EFu &&
        branch_generation == recorder->current_publication_generation &&
        branch_session == recorder->current_publication_session &&
        branch_sequence > recorder->current_publication_sequence &&
        branch_sequence < recorder->worker_sequence &&
        branch_host_address == host_address;
    if (!current_order_valid || !branch_identity_valid) {
        dsp_audio_causal_add(recorder->unmatched_channel_record_dmas);
        if (recorder->current_publication_blocked_by_selection_write) {
            dsp_audio_causal_add(
                recorder->dmas_after_intervening_selection);
        }
        if (!had_validated_branch) {
            dsp_audio_causal_add(
                recorder->dmas_without_validated_selected_channel_branch);
        } else {
            dsp_audio_causal_add(
                recorder->selected_channel_dma_identity_mismatches);
        }
        return;
    }

    dsp_audio_causal_add(
        recorder->validated_selected_channel_record_dmas);
    dsp_audio_causal_add(recorder->validated_channel_sequence);
    recorder->last_validated_channel = branch_channel;
    if (!recorder->current_publication_matched) {
        recorder->current_publication_matched = true;
        recorder->current_first_match_sequence = recorder->worker_sequence;
        recorder->current_first_match_dma_host_address = host_address;
        recorder->current_first_match_dma_pc = pc;
        recorder->current_first_match_channel = branch_channel;
        recorder->last_matched_publication_generation =
            recorder->current_publication_generation;
        recorder->last_matched_publication_session =
            recorder->current_publication_session;
        recorder->last_matched_channel = branch_channel;
        dsp_audio_causal_add(recorder->matched_active_publications);
    }
}

[[nodiscard]] inline DspChannelSelectionDmaProbeSnapshot
dsp_channel_selection_dma_probe_snapshot(
    const DspChannelSelectionDmaProbeRecorder& recorder) noexcept {
    return recorder;
}

inline void dsp_audio_causal_append(
    DspAudioCausalRecorder& recorder,
    DspAudioCausalEvent& event) {
    dsp_audio_causal_add(recorder.total_events);
    event.sequence = recorder.total_events;
    recorder.records[recorder.next_record] = event;
    recorder.next_record =
        (recorder.next_record + 1u) % kDspAudioCausalRecordCapacity;
    if (recorder.record_count < kDspAudioCausalRecordCapacity) {
        ++recorder.record_count;
    } else {
        dsp_audio_causal_add(recorder.overwritten_events);
    }
}

inline void dsp_audio_causal_record_dma(
    DspAudioCausalRecorder* recorder,
    std::uint16_t pc,
    std::uint16_t control,
    bool instruction_memory,
    bool to_host,
    std::uint32_t host_address,
    std::uint32_t dsp_byte_address,
    std::uint32_t length,
    std::uint32_t nonzero_bytes,
    std::span<const std::uint8_t> transfer_bytes = {}) {
    if (recorder == nullptr) {
        return;
    }
    DspAudioCausalEvent event{
        0u,
        dsp_audio_causal_classify_dma(
            to_host, instruction_memory, dsp_byte_address, length),
        pc,
        control,
        host_address,
        dsp_byte_address,
        length,
        nonzero_bytes};
    if (event.kind == DspAudioCausalEventKind::ChannelRecordIn &&
        transfer_bytes.size() >= 0x50u) {
        const auto read_be16 = [&transfer_bytes](std::size_t offset) {
            return static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(transfer_bytes[offset]) << 8u) |
                transfer_bytes[offset + 1u]);
        };
        bool selected_gain_tuple = false;
        for (std::uint8_t mixer = 0u; mixer < 8u; ++mixer) {
            const std::size_t offset =
                0x10u + static_cast<std::size_t>(mixer) * 8u;
            const std::uint16_t bus = read_be16(offset);
            const std::uint16_t current = read_be16(offset + 2u);
            const std::uint16_t target = read_be16(offset + 4u);
            const std::uint16_t delta = read_be16(offset + 6u);
            const bool tuple_nonzero =
                bus != 0u || current != 0u || target != 0u || delta != 0u;
            const bool gain_nonzero =
                current != 0u || target != 0u || delta != 0u;
            if (tuple_nonzero) {
                ++event.mixer_nonzero_tuples;
            }
            if (gain_nonzero) {
                ++event.mixer_nonzero_gain_tuples;
            }
            if ((!selected_gain_tuple && gain_nonzero) ||
                (event.mixer_first_index == 0xFFu && tuple_nonzero)) {
                event.mixer_first_index = mixer;
                event.mixer_bus = bus;
                event.mixer_current = current;
                event.mixer_target = target;
                event.mixer_delta = delta;
                selected_gain_tuple = gain_nonzero;
            }
        }
    }
    if (to_host) {
        dsp_audio_causal_add(recorder->dma_out_transfers);
        dsp_audio_causal_add(recorder->dma_out_bytes, length);
        dsp_audio_causal_add(
            recorder->dma_out_nonzero_bytes, nonzero_bytes);
    } else {
        dsp_audio_causal_add(recorder->dma_in_transfers);
        dsp_audio_causal_add(recorder->dma_in_bytes, length);
        dsp_audio_causal_add(
            recorder->dma_in_nonzero_bytes, nonzero_bytes);
    }
    switch (event.kind) {
    case DspAudioCausalEventKind::ChannelRecordIn:
        dsp_audio_causal_add(recorder->channel_record_transfers);
        if (nonzero_bytes != 0u) {
            dsp_audio_causal_add(
                recorder->channel_record_nonzero_transfers);
            dsp_audio_causal_add(
                recorder->channel_record_nonzero_bytes, nonzero_bytes);
        }
        if (transfer_bytes.size() >= 0x50u) {
            dsp_audio_causal_add(
                recorder->channel_mixer_inspected_transfers);
            if (event.mixer_nonzero_tuples != 0u) {
                dsp_audio_causal_add(
                    recorder->channel_mixer_nonzero_record_transfers);
            }
            dsp_audio_causal_add(
                recorder->channel_mixer_nonzero_tuples,
                event.mixer_nonzero_tuples);
            dsp_audio_causal_add(
                recorder->channel_mixer_nonzero_gain_tuples,
                event.mixer_nonzero_gain_tuples);
        }
        break;
    case DspAudioCausalEventKind::SampleWindowIn:
        dsp_audio_causal_add(recorder->sample_window_transfers);
        if (nonzero_bytes != 0u) {
            dsp_audio_causal_add(
                recorder->sample_window_nonzero_transfers);
            dsp_audio_causal_add(
                recorder->sample_window_nonzero_bytes, nonzero_bytes);
        }
        break;
    case DspAudioCausalEventKind::OutputBlockOut:
        dsp_audio_causal_add(recorder->output_block_transfers);
        if (nonzero_bytes != 0u) {
            dsp_audio_causal_add(
                recorder->output_block_nonzero_transfers);
            dsp_audio_causal_add(
                recorder->output_block_nonzero_bytes, nonzero_bytes);
        }
        break;
    default:
        break;
    }
    dsp_audio_causal_append(*recorder, event);
    if (to_host) {
        recorder->last_dma_out = event;
        recorder->has_last_dma_out = true;
    } else {
        recorder->last_dma_in = event;
        recorder->has_last_dma_in = true;
        if (nonzero_bytes != 0u) {
            recorder->last_nonzero_dma_in = event;
            recorder->has_last_nonzero_dma_in = true;
        }
    }
    switch (event.kind) {
    case DspAudioCausalEventKind::ChannelRecordIn:
        recorder->last_channel_record = event;
        recorder->has_last_channel_record = true;
        if (nonzero_bytes != 0u) {
            recorder->last_nonzero_channel_record = event;
            recorder->has_last_nonzero_channel_record = true;
        }
        if (event.mixer_nonzero_gain_tuples != 0u &&
            !recorder->has_first_routed_channel_record) {
            recorder->first_routed_channel_record = event;
            recorder->has_first_routed_channel_record = true;
        }
        break;
    case DspAudioCausalEventKind::SampleWindowIn:
        recorder->last_sample_window = event;
        recorder->has_last_sample_window = true;
        break;
    case DspAudioCausalEventKind::OutputBlockOut:
        recorder->last_output_block = event;
        recorder->has_last_output_block = true;
        break;
    default:
        break;
    }
}

inline void dsp_audio_causal_record_mixer_output_write(
    DspAudioCausalRecorder* recorder,
    std::uint16_t pc,
    std::uint16_t address,
    std::uint16_t value) {
    if (recorder == nullptr) {
        return;
    }
    const bool primary_mix_bus =
        (address >= 0x0D00u && address < 0x0D50u) ||
        (address >= 0x0D60u && address < 0x0DB0u);
    if (!(primary_mix_bus ||
          (address >= 0x0DC0u && address < 0x0E10u) ||
          (address >= 0x0E20u && address < 0x0E70u) ||
          (address >= 0x0E80u && address < 0x0ED0u) ||
          (address >= 0x0EE0u && address < 0x0F30u))) {
        return;
    }
    dsp_audio_causal_add(recorder->mixer_output_word_writes);
    if (primary_mix_bus) {
        dsp_audio_causal_add(recorder->primary_mix_bus_word_writes);
    }
    if (value == 0u) {
        return;
    }
    dsp_audio_causal_add(recorder->mixer_output_nonzero_word_writes);
    if (primary_mix_bus) {
        dsp_audio_causal_add(
            recorder->primary_mix_bus_nonzero_word_writes);
    }
    if (recorder->mixer_output_nonzero_word_writes == 1u) {
        DspAudioCausalEvent event{
            0u,
            DspAudioCausalEventKind::MixerOutputWrite,
            pc,
            value,
            0u,
            static_cast<std::uint32_t>(address) * 2u,
            2u,
            static_cast<std::uint32_t>(
                ((value & 0xFF00u) != 0u ? 1u : 0u) +
                ((value & 0x00FFu) != 0u ? 1u : 0u))};
        dsp_audio_causal_append(*recorder, event);
    }
}

inline void dsp_audio_causal_record_channel_selection_write(
    DspAudioCausalRecorder* recorder,
    std::uint16_t pc,
    std::uint16_t address,
    std::uint16_t value) {
    if (recorder == nullptr || pc != 0x0715u || address < 0x04FCu ||
        address > 0x04FFu) {
        return;
    }
    dsp_audio_causal_add(recorder->channel_selection_writes);
    if (value != 0u) {
        dsp_audio_causal_add(recorder->channel_selection_nonzero_writes);
        dsp_audio_causal_add(
            recorder->channel_selection_nonzero_bits,
            static_cast<std::uint64_t>(std::popcount(value)));
    }
    const auto index = static_cast<std::size_t>(address - 0x04FCu);
    recorder->channel_selection_words[index] = value;
    recorder->channel_selection_valid_mask = static_cast<std::uint8_t>(
        recorder->channel_selection_valid_mask |
        static_cast<std::uint8_t>(1u << index));
}

inline void dsp_audio_causal_record_accelerator_stopped(
    DspAudioCausalRecorder* recorder,
    std::uint16_t pc,
    std::uint32_t current_address) {
    if (recorder == nullptr) {
        return;
    }
    dsp_audio_causal_add(recorder->accelerator_calls);
    dsp_audio_causal_add(recorder->accelerator_stopped_calls);
    if (recorder->accelerator_stopped_calls == 1u) {
        DspAudioCausalEvent event{
            0u,
            DspAudioCausalEventKind::AcceleratorStopped,
            pc};
        event.accelerator_address = current_address;
        dsp_audio_causal_append(*recorder, event);
    }
}

inline void dsp_audio_causal_record_accelerator_sample(
    DspAudioCausalRecorder* recorder,
    std::uint16_t pc,
    std::uint16_t format,
    std::uint32_t current_address,
    std::int16_t raw_sample,
    std::uint16_t decoded_sample) {
    if (recorder == nullptr) {
        return;
    }
    dsp_audio_causal_add(recorder->accelerator_calls);
    dsp_audio_causal_add(recorder->accelerator_active_reads);
    DspAudioCausalEvent event{
        0u,
        DspAudioCausalEventKind::AcceleratorActive,
        pc,
        format};
    event.accelerator_address = current_address;
    event.raw_sample = raw_sample;
    event.decoded_sample = decoded_sample;
    if (recorder->accelerator_active_reads == 1u) {
        dsp_audio_causal_append(*recorder, event);
    }
    if (raw_sample != 0) {
        dsp_audio_causal_add(recorder->accelerator_raw_nonzero_reads);
        if (recorder->accelerator_raw_nonzero_reads == 1u) {
            auto raw_event = event;
            raw_event.kind = DspAudioCausalEventKind::AcceleratorRawNonzero;
            dsp_audio_causal_append(*recorder, raw_event);
        }
    }
    if (decoded_sample != 0u) {
        dsp_audio_causal_add(recorder->accelerator_decoded_nonzero_reads);
        if (recorder->accelerator_decoded_nonzero_reads == 1u) {
            auto decoded_event = event;
            decoded_event.kind =
                DspAudioCausalEventKind::AcceleratorDecodedNonzero;
            dsp_audio_causal_append(*recorder, decoded_event);
        }
    }
    event.sequence = recorder->total_events;
    recorder->last_accelerator_sample = event;
    recorder->has_last_accelerator_sample = true;
}

[[nodiscard]] inline DspAudioCausalSnapshot dsp_audio_causal_snapshot(
    const DspAudioCausalRecorder& recorder) noexcept {
    DspAudioCausalSnapshot snapshot{};
    snapshot.record_count = recorder.record_count;
    snapshot.total_events = recorder.total_events;
    snapshot.overwritten_events = recorder.overwritten_events;
    const std::size_t oldest =
        (recorder.next_record + kDspAudioCausalRecordCapacity -
         recorder.record_count) %
        kDspAudioCausalRecordCapacity;
    for (std::size_t index = 0u; index < recorder.record_count; ++index) {
        snapshot.records[index] = recorder.records[
            (oldest + index) % kDspAudioCausalRecordCapacity];
    }
#define GALAXY_COPY_AUDIO_CAUSAL_FIELD(name) snapshot.name = recorder.name
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(dma_in_transfers);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(dma_in_bytes);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(dma_in_nonzero_bytes);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(dma_out_transfers);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(dma_out_bytes);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(dma_out_nonzero_bytes);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(channel_record_transfers);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(channel_record_nonzero_transfers);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(channel_record_nonzero_bytes);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(channel_mixer_inspected_transfers);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(channel_mixer_nonzero_record_transfers);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(channel_mixer_nonzero_tuples);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(channel_mixer_nonzero_gain_tuples);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(channel_selection_writes);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(channel_selection_nonzero_writes);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(channel_selection_nonzero_bits);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(channel_selection_valid_mask);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(channel_selection_words);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(sample_window_transfers);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(sample_window_nonzero_transfers);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(sample_window_nonzero_bytes);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(mixer_output_word_writes);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(mixer_output_nonzero_word_writes);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(primary_mix_bus_word_writes);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(primary_mix_bus_nonzero_word_writes);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(output_block_transfers);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(output_block_nonzero_transfers);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(output_block_nonzero_bytes);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(accelerator_calls);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(accelerator_stopped_calls);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(accelerator_active_reads);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(accelerator_raw_nonzero_reads);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(accelerator_decoded_nonzero_reads);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(has_last_dma_in);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(has_last_nonzero_dma_in);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(has_last_dma_out);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(has_last_channel_record);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(has_first_routed_channel_record);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(has_last_nonzero_channel_record);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(has_last_sample_window);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(has_last_output_block);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(has_last_accelerator_sample);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(last_dma_in);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(last_nonzero_dma_in);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(last_dma_out);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(last_channel_record);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(first_routed_channel_record);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(last_nonzero_channel_record);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(last_sample_window);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(last_output_block);
    GALAXY_COPY_AUDIO_CAUSAL_FIELD(last_accelerator_sample);
#undef GALAXY_COPY_AUDIO_CAUSAL_FIELD
    return snapshot;
}

[[nodiscard]] inline constexpr DspAudioCausalClassification
dsp_audio_causal_classify(
    const DspAudioCausalSnapshot& snapshot) noexcept {
    if (snapshot.dma_in_transfers == 0u) {
        return DspAudioCausalClassification::NoDmaInput;
    }
    if (snapshot.dma_in_nonzero_bytes == 0u) {
        return DspAudioCausalClassification::DmaInputSilent;
    }
    if (snapshot.channel_record_transfers == 0u) {
        return DspAudioCausalClassification::NoChannelRecord;
    }
    if (snapshot.channel_record_nonzero_transfers == 0u) {
        return DspAudioCausalClassification::ChannelRecordSilent;
    }
    if (snapshot.channel_mixer_inspected_transfers !=
        snapshot.channel_record_transfers) {
        return DspAudioCausalClassification::ChannelRoutingUnobserved;
    }
    if (snapshot.channel_mixer_nonzero_gain_tuples == 0u) {
        return DspAudioCausalClassification::ChannelRoutingSilent;
    }
    const bool sample_source_seen =
        snapshot.sample_window_transfers != 0u ||
        snapshot.accelerator_active_reads != 0u;
    if (!sample_source_seen) {
        return DspAudioCausalClassification::NoSampleSource;
    }
    const bool sample_source_nonzero =
        snapshot.sample_window_nonzero_transfers != 0u ||
        snapshot.accelerator_decoded_nonzero_reads != 0u;
    if (!sample_source_nonzero) {
        return DspAudioCausalClassification::SampleSourceSilent;
    }
    if (snapshot.mixer_output_word_writes == 0u) {
        return DspAudioCausalClassification::MixerOutputUnwritten;
    }
    if (snapshot.mixer_output_nonzero_word_writes == 0u) {
        return DspAudioCausalClassification::MixerOutputSilent;
    }
    if (snapshot.output_block_transfers == 0u) {
        return DspAudioCausalClassification::OutputDmaAbsent;
    }
    if (snapshot.output_block_nonzero_transfers == 0u) {
        return DspAudioCausalClassification::OutputDmaSilent;
    }
    return DspAudioCausalClassification::OutputObserved;
}

[[nodiscard]] inline constexpr const char*
dsp_audio_causal_classification_name(
    DspAudioCausalClassification classification) noexcept {
    switch (classification) {
    case DspAudioCausalClassification::NoDmaInput:
        return "no-dma-input";
    case DspAudioCausalClassification::DmaInputSilent:
        return "dma-input-silent";
    case DspAudioCausalClassification::NoChannelRecord:
        return "no-channel-record";
    case DspAudioCausalClassification::ChannelRecordSilent:
        return "channel-record-silent";
    case DspAudioCausalClassification::ChannelRoutingUnobserved:
        return "channel-routing-unobserved";
    case DspAudioCausalClassification::ChannelRoutingSilent:
        return "channel-routing-silent";
    case DspAudioCausalClassification::NoSampleSource:
        return "no-sample-source";
    case DspAudioCausalClassification::SampleSourceSilent:
        return "sample-source-silent";
    case DspAudioCausalClassification::MixerOutputUnwritten:
        return "mixer-output-unwritten";
    case DspAudioCausalClassification::MixerOutputSilent:
        return "mixer-output-silent";
    case DspAudioCausalClassification::OutputDmaAbsent:
        return "output-dma-absent";
    case DspAudioCausalClassification::OutputDmaSilent:
        return "output-dma-silent";
    case DspAudioCausalClassification::OutputObserved:
        return "output-observed";
    }
    return "invalid";
}

inline void dsp_native_telemetry_add(
    std::atomic<std::uint64_t>& counter,
    std::uint64_t amount = 1u) {
    if (amount == 0u) {
        return;
    }
    auto current = counter.load(std::memory_order_relaxed);
    for (;;) {
        if (amount > std::numeric_limits<std::uint64_t>::max() - current) {
            std::abort();
        }
        if (counter.compare_exchange_weak(
                current,
                current + amount,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            return;
        }
    }
}

inline DspNativeTelemetrySnapshot dsp_native_telemetry_snapshot(
    const DspNativeTelemetryState& state) {
    std::uint64_t retired_instructions = 0u;
    std::uint64_t zero_queue_idle_backedges = 0u;
    std::uint64_t command_wait_idle_backedges = 0u;
    std::uint64_t zero_queue_short_reentries = 0u;
    std::uint64_t zero_queue_complete_sequences = 0u;
    std::uint64_t command_wait_complete_sequences = 0u;
    bool coherent = false;
    // This is optional evidence. A preempted publisher must never trap the
    // simulation/diagnostic owner in an unbounded reader spin.
    for (unsigned attempt = 0; attempt < 16u; ++attempt) {
        const auto sequence_before =
            state.worker_publication_sequence.load(std::memory_order_acquire);
        if ((sequence_before & 1u) != 0u) {
            continue;
        }
        retired_instructions =
            state.retired_instructions.load(std::memory_order_acquire);
        zero_queue_idle_backedges =
            state.zero_queue_idle_backedges.load(std::memory_order_acquire);
        command_wait_idle_backedges =
            state.command_wait_idle_backedges.load(std::memory_order_acquire);
        zero_queue_short_reentries =
            state.zero_queue_short_reentries.load(std::memory_order_acquire);
        zero_queue_complete_sequences =
            state.zero_queue_complete_sequences.load(std::memory_order_acquire);
        command_wait_complete_sequences =
            state.command_wait_complete_sequences.load(std::memory_order_acquire);
        const auto sequence_after =
            state.worker_publication_sequence.load(std::memory_order_acquire);
        if (sequence_before == sequence_after) {
            coherent = true;
            break;
        }
    }
    if (!coherent) {
        retired_instructions = state.retired_instructions.load(std::memory_order_acquire);
        zero_queue_idle_backedges = state.zero_queue_idle_backedges.load(std::memory_order_acquire);
        command_wait_idle_backedges = state.command_wait_idle_backedges.load(std::memory_order_acquire);
        zero_queue_short_reentries = state.zero_queue_short_reentries.load(std::memory_order_acquire);
        zero_queue_complete_sequences = state.zero_queue_complete_sequences.load(std::memory_order_acquire);
        command_wait_complete_sequences = state.command_wait_complete_sequences.load(std::memory_order_acquire);
    }
    auto snapshot = DspNativeTelemetrySnapshot{
        retired_instructions,
        zero_queue_idle_backedges,
        command_wait_idle_backedges,
        zero_queue_short_reentries,
        zero_queue_complete_sequences,
        command_wait_complete_sequences,
        state.zero_queue_idle_waits.load(std::memory_order_acquire),
        state.command_wait_idle_waits.load(std::memory_order_acquire),
        state.cpu_mail_published.load(std::memory_order_acquire),
        state.cpu_mail_consumed.load(std::memory_order_acquire),
        state.dsp_mail_published.load(std::memory_order_acquire),
        state.dsp_mail_consumed.load(std::memory_order_acquire),
        state.dirq_writes.load(std::memory_order_acquire),
        state.dma_in_transfers.load(std::memory_order_acquire),
        state.dma_in_bytes.load(std::memory_order_acquire),
        state.dma_in_nonzero_bytes.load(std::memory_order_acquire),
        state.dma_out_transfers.load(std::memory_order_acquire),
        state.dma_out_bytes.load(std::memory_order_acquire),
        state.dma_out_nonzero_bytes.load(std::memory_order_acquire),
    };
    snapshot.worker_counters_coherent = coherent;
    return snapshot;
}

struct DspRmge01IdleSequenceTracker;

inline void dsp_rmge01_idle_sequence_observe_marker(
    DspRmge01IdleSequenceTracker* tracker,
    std::uint16_t previous_pc,
    std::uint16_t entered_pc) noexcept;

// This wrapper preserves the generated source ABI (`ctx.last_retired_pc =
// ...`) while making each existing marker contribute to native liveness
// telemetry. The pending accumulator belongs to the one DSP execution thread;
// it is published atomically only at observable hardware boundaries, avoiding
// an atomic operation for every generated instruction.
struct DspInstructionEntryMarker {
    std::uint16_t pc{};
    std::uint64_t* pending_entries{};
    DspRmge01IdleSequenceTracker* idle_sequence_tracker{};

    DspInstructionEntryMarker& operator=(std::uint16_t value) noexcept {
        const auto previous_pc = pc;
        pc = value;
        if (pending_entries != nullptr) {
            if (*pending_entries == std::numeric_limits<std::uint64_t>::max()) {
                std::abort();
            }
            ++*pending_entries;
        }
        dsp_rmge01_idle_sequence_observe_marker(
            idle_sequence_tracker, previous_pc, value);
        return *this;
    }

    operator std::uint16_t() const noexcept { return pc; }
};

struct DspHardTrap {
    std::uint16_t pc{};
    const char* reason{};
    // Address of the last instruction that actually retired before the trap.
    // For a computed-branch trap this is the source instruction whose
    // register/table computation produced the out-of-range target — pc alone
    // only reports the bad destination, not what jumped there.
    std::uint16_t last_retired_pc{};
};

inline constexpr std::size_t kDspAddressRegisterCount = 4;
inline constexpr std::size_t kDspIndexRegisterCount = 4;
inline constexpr std::size_t kDspWrapRegisterCount = 4;
inline constexpr std::size_t kDspAxRegisterCount = 2;
inline constexpr std::size_t kDspAccumulatorCount = 2;
inline constexpr std::size_t kDspStackRegisterCount = 4;
inline constexpr std::size_t kDspStackWords = 32;
inline constexpr std::uint8_t kDspStackMask = 0x1f;
inline constexpr std::size_t kDspIramWords = 0x1000;
inline constexpr std::size_t kDspIromWords = 0x1000;
inline constexpr std::size_t kDspDramWords = 0x1000;
inline constexpr std::size_t kDspCoefWords = 0x800;
inline constexpr std::size_t kDspIfxRegisterWords = 0x100;

inline constexpr std::uint16_t kDspIfxCoefA1Base = 0xa0;
inline constexpr std::uint16_t kDspIfxDscr = 0xc9;
inline constexpr std::uint16_t kDspIfxDsbl = 0xcb;
inline constexpr std::uint16_t kDspIfxDspa = 0xcd;
inline constexpr std::uint16_t kDspIfxDsmah = 0xce;
inline constexpr std::uint16_t kDspIfxDsmal = 0xcf;
inline constexpr std::uint16_t kDspIfxFormat = 0xd1;
inline constexpr std::uint16_t kDspIfxAcdraw = 0xd3;
inline constexpr std::uint16_t kDspIfxAcsah = 0xd4;
inline constexpr std::uint16_t kDspIfxAcsal = 0xd5;
inline constexpr std::uint16_t kDspIfxAceah = 0xd6;
inline constexpr std::uint16_t kDspIfxAceal = 0xd7;
inline constexpr std::uint16_t kDspIfxAccah = 0xd8;
inline constexpr std::uint16_t kDspIfxAccal = 0xd9;
inline constexpr std::uint16_t kDspIfxPredScale = 0xda;
inline constexpr std::uint16_t kDspIfxYn1 = 0xdb;
inline constexpr std::uint16_t kDspIfxYn2 = 0xdc;
inline constexpr std::uint16_t kDspIfxAcdsamp = 0xdd;
inline constexpr std::uint16_t kDspIfxGain = 0xde;
inline constexpr std::uint16_t kDspIfxAcin = 0xdf;
inline constexpr std::uint16_t kDspIfxAmdm = 0xef;
inline constexpr std::uint16_t kDspIfxDirq = 0xfb;
inline constexpr std::uint16_t kDspIfxDmbh = 0xfc;
inline constexpr std::uint16_t kDspIfxDmbl = 0xfd;
inline constexpr std::uint16_t kDspIfxCmbh = 0xfe;
inline constexpr std::uint16_t kDspIfxCmbl = 0xff;
inline constexpr std::uint32_t kDspMailboxBusy = 0x80000000u;
inline constexpr std::uint16_t kDspSrExtIntEnable = 0x0800u;
inline constexpr std::uint16_t kDspNoPendingExternalInterrupt = 0xffffu;

// A diagnostic sample is a bounded copy of the worker-owned state needed by
// native-host traces. It deliberately does not expose DspContext: CPU-side
// diagnostics must never inspect the live register file or DRAM while lowered
// DSP code is executing. The selected ranges cover the boot/interrupt probes,
// the JAudio command/work areas, and the release-slot trace without copying all
// 8 KiB of DSP DRAM at each observable boundary.
inline constexpr std::uint16_t kDspDiagnosticLowDramStart = 0x0000u;
inline constexpr std::size_t kDspDiagnosticLowDramWords = 0x0060u;
inline constexpr std::uint16_t kDspDiagnosticCommandRingStart = 0x0280u;
inline constexpr std::size_t kDspDiagnosticCommandRingWords = 0x0040u;
inline constexpr std::uint16_t kDspDiagnosticWorkScratchStart = 0x0350u;
inline constexpr std::size_t kDspDiagnosticWorkScratchWords = 0x0060u;
inline constexpr std::uint16_t kDspDiagnosticReleaseSlotsStart = 0x04FCu;
inline constexpr std::size_t kDspDiagnosticReleaseSlotsWords = 0x0050u;
static_assert(
    static_cast<std::size_t>(kDspDiagnosticLowDramStart) +
            kDspDiagnosticLowDramWords <=
        kDspDramWords);
static_assert(
    static_cast<std::size_t>(kDspDiagnosticCommandRingStart) +
            kDspDiagnosticCommandRingWords <=
        kDspDramWords);
static_assert(
    static_cast<std::size_t>(kDspDiagnosticWorkScratchStart) +
            kDspDiagnosticWorkScratchWords <=
        kDspDramWords);
static_assert(
    static_cast<std::size_t>(kDspDiagnosticReleaseSlotsStart) +
            kDspDiagnosticReleaseSlotsWords <=
        kDspDramWords);
static_assert(
    0x0381u >= kDspDiagnosticWorkScratchStart &&
    0x0381u <
        static_cast<std::size_t>(kDspDiagnosticWorkScratchStart) +
            kDspDiagnosticWorkScratchWords);

enum class DspDiagnosticBoundary : std::uint8_t {
    None,
    DspMailboxLow,
    DspInterruptRequest,
    HaltReturn,
    ExternalInterruptReturn,
    AbortReturn,
    HardTrapReturn,
    UnexpectedReturn,
    SingleShotReturn,
    QuiescentWorkerQuery,
};

struct DspDiagnosticSnapshot {
    std::uint64_t publication_generation{};
    std::uint64_t dsp_mail_generation{};
    std::uint64_t dirq_generation{};
    std::uint64_t return_generation{};
    std::uint64_t worker_completed_runs{};
    DspDiagnosticBoundary boundary{DspDiagnosticBoundary::None};
    std::uint16_t pc{};
    std::uint16_t sr{};
    std::array<std::uint16_t, kDspStackRegisterCount> st{};
    bool halted{};
    // These shared words are captured through atomic_ref, never through a
    // plain DspContext copy.
    std::uint32_t dsp_mailbox{};
    std::uint32_t cpu_mailbox{};
    std::array<std::uint16_t, kDspDiagnosticLowDramWords> low_dram{};
    std::array<std::uint16_t, kDspDiagnosticCommandRingWords> command_ring{};
    std::array<std::uint16_t, kDspDiagnosticWorkScratchWords> work_scratch{};
    std::array<std::uint16_t, kDspDiagnosticReleaseSlotsWords> release_slots{};

    [[nodiscard]] bool read_dram_word(
        std::uint16_t address,
        std::uint16_t& value) const noexcept {
        const auto read_range =
            [address, &value](std::uint16_t first, const auto& words) {
                if (address < first) {
                    return false;
                }
                const auto offset = static_cast<std::size_t>(address - first);
                if (offset >= words.size()) {
                    return false;
                }
                value = words[offset];
                return true;
            };
        return read_range(kDspDiagnosticLowDramStart, low_dram) ||
               read_range(kDspDiagnosticCommandRingStart, command_ring) ||
               read_range(kDspDiagnosticWorkScratchStart, work_scratch) ||
               read_range(kDspDiagnosticReleaseSlotsStart, release_slots);
    }

    [[nodiscard]] std::uint32_t channel_table_address() const noexcept {
        constexpr std::size_t kHigh = 0x0380u - kDspDiagnosticWorkScratchStart;
        constexpr std::size_t kLow = 0x0381u - kDspDiagnosticWorkScratchStart;
        return (static_cast<std::uint32_t>(work_scratch[kHigh]) << 16u) |
               work_scratch[kLow];
    }
};

// Owned by NativeDspCoprocessor. The worker is the only publisher during an
// active session; CPU readers copy latest while holding this mutex, then drop
// it before formatting or scanning host memory. A pointer is installed in
// DspContext only when diagnostics are enabled, leaving the normal product
// path with one null check and no copy/lock at hardware boundaries.
struct DspDiagnosticState {
    mutable std::mutex mutex{};
    DspDiagnosticSnapshot latest{};
    std::uint64_t publication_generation{};
    std::uint64_t dsp_mail_generation{};
    std::uint64_t dirq_generation{};
    std::uint64_t return_generation{};
    bool valid{};
};

// Generated RMGE01 ucode has five CMBH reads. Four are explicit empty-mailbox
// back-edge loops; PC 0x06F5 is a non-blocking test whose empty result branches
// to task-dispatch housekeeping at 0x07EA. It also has two DMBH reads in
// explicit busy-mailbox back-edge loops. Keep this classification fail-closed:
// every unknown PC retains immediate register-read semantics.
enum class DspRmge01MailboxPollKind : std::uint8_t {
    None,
    CmbhEmptyBackEdge,
    DmbhBusyBackEdge,
};

// These two direct back-edges close statically audited RMGE01 SCCs whose
// bodies perform no IFX writes, DMA, mailbox operations, or DRAM writes.  The
// first reloads D[0x0351]/D[0x0352]; the second reloads D[0x0354]/D[0x034E].
// Only the DSP interrupt handler can change those worker-owned words while
// the generated DSP thread is executing. Keep the pair classifier exact and
// fail closed: neighboring or dynamically computed edges are ordinary work.
enum class DspRmge01IdleBackEdgeKind : std::uint8_t {
    None,
    ZeroQueue,
    CommandWait,
};

inline constexpr std::uint16_t kRmge01DspZeroQueueIdleSourcePc = 0x0820u;
inline constexpr std::uint16_t kRmge01DspZeroQueueIdleTargetPc = 0x002Bu;
inline constexpr std::uint16_t kRmge01DspCommandWaitIdleSourcePc = 0x029Au;
inline constexpr std::uint16_t kRmge01DspCommandWaitIdleTargetPc = 0x0293u;
inline constexpr std::uint16_t kRmge01DspZeroQueueShortReentrySourcePc =
    0x0824u;
inline constexpr std::uint16_t kRmge01DspZeroQueueShortReentryTargetPc =
    0x07FAu;

inline constexpr std::array<std::uint16_t, 8>
    kRmge01DspZeroQueueIdleSequence{
        0x002Bu, 0x07F6u, 0x07F8u, 0x07FAu,
        0x07FCu, 0x07FDu, 0x081Eu, 0x0820u};
inline constexpr std::array<std::uint16_t, 6>
    kRmge01DspCommandWaitIdleSequence{
        0x0293u, 0x0294u, 0x0295u, 0x0297u, 0x0299u, 0x029Au};
static_assert(
    kRmge01DspZeroQueueIdleSequence.front() ==
        kRmge01DspZeroQueueIdleTargetPc &&
    kRmge01DspZeroQueueIdleSequence.back() ==
        kRmge01DspZeroQueueIdleSourcePc &&
    kRmge01DspZeroQueueIdleSequence.size() <=
        static_cast<std::size_t>(
            std::numeric_limits<std::uint8_t>::max()));
static_assert(
    kRmge01DspCommandWaitIdleSequence.front() ==
        kRmge01DspCommandWaitIdleTargetPc &&
    kRmge01DspCommandWaitIdleSequence.back() ==
        kRmge01DspCommandWaitIdleSourcePc &&
    kRmge01DspCommandWaitIdleSequence.size() <=
        static_cast<std::size_t>(
            std::numeric_limits<std::uint8_t>::max()));

struct DspRmge01IdleSequenceTracker {
    DspRmge01IdleBackEdgeKind kind{DspRmge01IdleBackEdgeKind::None};
    std::uint8_t progress{};
};

inline void dsp_rmge01_idle_sequence_reset(
    DspRmge01IdleSequenceTracker* tracker) noexcept {
    if (tracker != nullptr) {
        *tracker = {};
    }
}

[[nodiscard]] inline constexpr bool dsp_rmge01_idle_sequence_ready(
    const DspRmge01IdleSequenceTracker& tracker,
    DspRmge01IdleBackEdgeKind kind) noexcept {
    switch (kind) {
    case DspRmge01IdleBackEdgeKind::ZeroQueue:
        return tracker.kind == kind &&
               static_cast<std::size_t>(tracker.progress) ==
                   kRmge01DspZeroQueueIdleSequence.size();
    case DspRmge01IdleBackEdgeKind::CommandWait:
        return tracker.kind == kind &&
               static_cast<std::size_t>(tracker.progress) ==
                   kRmge01DspCommandWaitIdleSequence.size();
    case DspRmge01IdleBackEdgeKind::None:
        return false;
    }
    return false;
}

inline void dsp_rmge01_idle_sequence_observe_marker(
    DspRmge01IdleSequenceTracker* tracker,
    std::uint16_t previous_pc,
    std::uint16_t entered_pc) noexcept {
    if (tracker == nullptr) {
        return;
    }

    if (entered_pc == kRmge01DspZeroQueueIdleSequence.front()) {
        tracker->kind = DspRmge01IdleBackEdgeKind::ZeroQueue;
        tracker->progress = 1u;
        return;
    }
    if (entered_pc == kRmge01DspCommandWaitIdleSequence.front()) {
        tracker->kind = DspRmge01IdleBackEdgeKind::CommandWait;
        tracker->progress = 1u;
        return;
    }

    const auto advance = [tracker, previous_pc, entered_pc](
                             DspRmge01IdleBackEdgeKind kind,
                             const auto& sequence) noexcept {
        const auto progress = static_cast<std::size_t>(tracker->progress);
        if (tracker->kind != kind || progress == 0u ||
            progress >= sequence.size() ||
            previous_pc != sequence[progress - 1u] ||
            entered_pc != sequence[progress]) {
            return false;
        }
        ++tracker->progress;
        return true;
    };

    if ((tracker->kind == DspRmge01IdleBackEdgeKind::ZeroQueue &&
         advance(
             DspRmge01IdleBackEdgeKind::ZeroQueue,
             kRmge01DspZeroQueueIdleSequence)) ||
        (tracker->kind == DspRmge01IdleBackEdgeKind::CommandWait &&
         advance(
             DspRmge01IdleBackEdgeKind::CommandWait,
             kRmge01DspCommandWaitIdleSequence))) {
        return;
    }
    dsp_rmge01_idle_sequence_reset(tracker);
}

[[nodiscard]] inline constexpr DspRmge01IdleBackEdgeKind
dsp_classify_rmge01_idle_backedge(
    std::uint16_t source,
    std::uint16_t target) noexcept {
    if (source == kRmge01DspZeroQueueIdleSourcePc &&
        target == kRmge01DspZeroQueueIdleTargetPc) {
        return DspRmge01IdleBackEdgeKind::ZeroQueue;
    }
    if (source == kRmge01DspCommandWaitIdleSourcePc &&
        target == kRmge01DspCommandWaitIdleTargetPc) {
        return DspRmge01IdleBackEdgeKind::CommandWait;
    }
    return DspRmge01IdleBackEdgeKind::None;
}

struct DspPendingIdleBackEdgeCounters {
    std::uint64_t zero_queue{};
    std::uint64_t command_wait{};
    std::uint64_t zero_queue_short_reentries{};
    std::uint64_t zero_queue_complete_sequences{};
    std::uint64_t command_wait_complete_sequences{};
};

inline constexpr std::uint16_t kRmge01DspCmbhCommandPollPc = 0x0700u;
inline constexpr std::uint16_t kRmge01DspCmbhAc0PollPc = 0x078Fu;
inline constexpr std::uint16_t kRmge01DspCmbhAc1PollPc = 0x0795u;
inline constexpr std::uint16_t kRmge01DspCmbhTaskPollPc = 0x07CCu;
inline constexpr std::uint16_t kRmge01DspDmbhAc0PollPc = 0x07ADu;
inline constexpr std::uint16_t kRmge01DspDmbhTaskPollPc = 0x07B3u;

[[nodiscard]] inline constexpr DspRmge01MailboxPollKind
dsp_classify_rmge01_mailbox_poll_pc(std::uint16_t pc) noexcept {
    switch (pc) {
    case kRmge01DspCmbhCommandPollPc:
    case kRmge01DspCmbhAc0PollPc:
    case kRmge01DspCmbhAc1PollPc:
    case kRmge01DspCmbhTaskPollPc:
        return DspRmge01MailboxPollKind::CmbhEmptyBackEdge;
    case kRmge01DspDmbhAc0PollPc:
    case kRmge01DspDmbhTaskPollPc:
        return DspRmge01MailboxPollKind::DmbhBusyBackEdge;
    default:
        return DspRmge01MailboxPollKind::None;
    }
}

[[nodiscard]] inline constexpr bool dsp_is_rmge01_cmbh_poll_pc(
    std::uint16_t pc) noexcept {
    return dsp_classify_rmge01_mailbox_poll_pc(pc) ==
           DspRmge01MailboxPollKind::CmbhEmptyBackEdge;
}

[[nodiscard]] inline constexpr bool dsp_is_rmge01_dmbh_poll_pc(
    std::uint16_t pc) noexcept {
    return dsp_classify_rmge01_mailbox_poll_pc(pc) ==
           DspRmge01MailboxPollKind::DmbhBusyBackEdge;
}

struct DspContext;
class DspRawInstructionBoundaryObserver;

// Optional, test-owned logical stack evidence. Hardware stack cursors wrap at
// five bits and are not depths. This sidecar may be attached only at a known
// fresh-reset boundary; ordinary product contexts leave the pointer null.
// Losing representability is sticky evidence failure only and must never alter
// hardware push/pop behavior.
enum class DspLogicalStackDepthAvailability : std::uint8_t {
    Unavailable,
    Exact,
};

struct DspLogicalStackDepthTracker {
    const DspContext* owner{};
    std::uint64_t epoch{};
    std::array<std::uint8_t, kDspStackRegisterCount> depths{};
    std::array<
        DspLogicalStackDepthAvailability,
        kDspStackRegisterCount>
        availability{};
};

enum class DspAcceleratorException : std::uint8_t {
    RawReadEnd,
    RawWriteEnd,
    SampleReadEnd,
};

struct DspHardwareServices {
    void (*request_interrupt)(void* user) {};
    // Internal native-runtime ordering hooks. These are deliberately not part
    // of NativeServicesV1: generated DSP code reaches them only through IFX
    // hardware operations. A busy CMBL consume hook must perform the actual
    // mailbox low read while any generation-bound host packet remains latched,
    // and must report that the busy mail was really consumed. Publication
    // hooks run before DMBL/DIRQ become guest-visible; returning false is a DSP
    // hard trap, never a best-effort fallback.
    bool (*consume_cpu_mailbox_low)(
        void* user,
        std::uint64_t generation,
        std::uint32_t* mailbox,
        std::uint16_t* value,
        bool* consumed_mail) {};
    bool (*before_dsp_mailbox_low)(void* user) {};
    bool (*before_dsp_interrupt)(void* user) {};
    bool (*before_clean_halt_return)(void* user) {};
    // DSP DMA bus (DSMAH/DSMAL): targets main memory (MRAM).
    // validate_external_span is called once before a DMA starts so a rejected
    // transfer cannot partially update either side before the failing byte is
    // discovered. Byte callbacks still report failure independently: the host
    // mapping may become unavailable after validation, and that must remain a
    // DSP hard trap rather than fabricated zero data or a dropped write.
    bool (*validate_external_span)(
        void* user,
        std::uint32_t address,
        std::uint32_t size) {};
    bool (*external_read_byte)(
        void* user,
        std::uint32_t address,
        std::uint8_t* value) {};
    bool (*external_write_byte)(
        void* user,
        std::uint32_t address,
        std::uint8_t value) {};
    // Production DMA uses one whole-span transaction. The DSP execution
    // thread supplies or receives a private, hardware-bounded buffer and the
    // host acknowledges only after the CPU/simulation thread has committed
    // the complete span. Byte callbacks above remain an explicit
    // single-threaded/test bridge. Product hosts install only these span
    // callbacks, leaving the byte callbacks null, so generated DSP code cannot
    // alias live MRAM through a second path.
    bool (*external_read_span)(
        void* user,
        std::uint32_t address,
        std::uint8_t* destination,
        std::uint32_t size) {};
    bool (*external_write_span)(
        void* user,
        std::uint32_t address,
        const std::uint8_t* source,
        std::uint32_t size) {};
    // Sample-accelerator bus (ACSAH/ACCAH/...): targets ARAM. On real hardware
    // this is a physically distinct bus from DSP DMA; their address spaces
    // overlap numerically, so the host must be able to route them separately.
    // Default-wired to the same backing as the DMA bus so single-bus callers
    // (and the bit-exact tests) behave exactly as before.
    bool (*validate_aram_span)(
        void* user,
        std::uint32_t address,
        std::uint32_t size) {};
    bool (*aram_read_byte)(
        void* user,
        std::uint32_t address,
        std::uint8_t* value) {};
    bool (*aram_write_byte)(
        void* user,
        std::uint32_t address,
        std::uint8_t value) {};
    // PCM16 and raw-word accelerator operations are one architectural word,
    // including a page-crossing word. Product hosts install these callbacks so
    // the operation cannot be split into two independently visible byte calls.
    bool (*aram_read_u16)(
        void* user,
        std::uint32_t address,
        std::uint16_t* value) {};
    bool (*aram_write_u16)(
        void* user,
        std::uint32_t address,
        std::uint16_t value) {};
    void (*accelerator_exception)(void* user, DspAcceleratorException exception) {};
};

struct DspContext {
    std::array<std::uint16_t, kDspAddressRegisterCount> ar{};
    std::array<std::int16_t, kDspIndexRegisterCount> ix{};
    std::array<std::uint16_t, kDspWrapRegisterCount> wr{};
    std::array<std::array<std::uint16_t, 2>, kDspAxRegisterCount> ax{};
    std::array<std::int64_t, kDspAccumulatorCount> ac{};
    DspProductParts prod{};
    std::array<std::uint16_t, kDspStackRegisterCount> st{};
    std::array<std::array<std::uint16_t, kDspStackWords>, kDspStackRegisterCount> stack{};
    std::array<std::uint8_t, kDspStackRegisterCount> stack_pointers{};
    std::uint16_t cr{};
    std::uint16_t sr{};
    std::uint16_t pc{};
    std::array<std::uint16_t, kDspIramWords> iram{};
    std::array<std::uint16_t, kDspIromWords> irom{};
    std::array<std::uint16_t, kDspDramWords> dram{};
    std::array<std::uint16_t, kDspCoefWords> coef{};
    std::array<std::uint16_t, kDspIfxRegisterWords> ifx{};
    DspHardwareServices hardware{};
    void* hardware_user{};
    std::uint32_t dsp_mailbox{};
    std::uint32_t cpu_mailbox{};
    // Set by the host when a free-running entry must stop. Owned elsewhere (the
    // coprocessor), so this is a plain copyable pointer that survives the
    // value-copy in reset(); the mailbox-poll path checks it and throws
    // DspExecutionAborted. Null for ordinary single-threaded execution.
    std::atomic<bool>* host_abort{};
    // Monotonic identity published by the CPU before releasing the low half of
    // a busy CPU->DSP mailbox. The CMBL ordering hook consumes the exact packet
    // bound to this generation.
    std::atomic<std::uint64_t>* host_cpu_mail_generation{};
    // Host-owned pending external interrupt vector. The CPU asserts this via
    // DSPCR PIINT; compiled ucode accepts it at IFX/mailbox boundaries and then
    // re-enters through the native vector with RTI stack state preserved.
    std::atomic<std::uint16_t>* host_external_interrupt{};
    DspHostWakeState* host_wake{};
    DspNativeTelemetryState* host_telemetry{};
    // Installed only for GALAXY_TRACE_AUDIO_CAUSAL. The native DSP worker is
    // the sole writer and the host snapshots it only after joining that
    // worker, so hot-path evidence collection needs no synchronization.
    DspAudioCausalRecorder* audio_causal_recorder{};
    // Installed only for GALAXY_TRACE_DSP_SELECTION_DMA_CAUSAL. Selection
    // writes call this from one statically lowered RMGE01 instruction; DMA
    // completion performs one null/exact-shape check. Worker-owned and copied
    // only after join.
    DspChannelSelectionDmaProbeRecorder* channel_selection_dma_probe{};
    // Optional process-owned bus controls. Exact selected-channel DMA proof
    // chooses the worker-local ramp; accelerator sample reads never lock or
    // touch atomics after that one record boundary snapshot.
    const audio::NativeAudioBusControls* audio_bus_controls{};
    audio::NativeDspAudioBusState audio_bus_state{};
    // Worker-owned, non-atomic accumulators. Observable boundaries publish
    // them into host_telemetry so the hot direct edges do not perform an
    // atomic operation on every iteration.
    DspPendingIdleBackEdgeCounters* pending_idle_backedges{};
    DspDiagnosticState* host_diagnostics{};
    // Optional non-owning test observer. Product construction/reset leaves this
    // null, and no native worker or host API installs it.
    DspRawInstructionBoundaryObserver* raw_instruction_boundary_observer{};
    // Optional non-owning test sidecar, enabled only from a known fresh reset.
    // The null product path pays one pointer load and one predictable
    // not-taken branch only when a hardware stack push/pop actually occurs.
    DspLogicalStackDepthTracker* logical_stack_depth_tracker{};
    bool halted{};
    bool irom_loaded{};
    bool coef_loaded{};
    bool accelerator_reads_stopped{};
    // Address of the currently-executing instruction's own block, refreshed at
    // the start of every lowered instruction. Diagnostics-only: lets a
    // computed-branch hard trap report which instruction computed the bad
    // target, since by the time the trap fires ctx.pc already holds the
    // (invalid) destination rather than the source.
    DspInstructionEntryMarker last_retired_pc{};
};

inline void dsp_native_reset_idle_sequence_progress(
    DspContext& context) noexcept {
    dsp_rmge01_idle_sequence_reset(
        context.last_retired_pc.idle_sequence_tracker);
}

inline DspRmge01IdleBackEdgeKind dsp_native_record_static_backedge(
    DspContext& context,
    std::uint16_t source,
    std::uint16_t target) noexcept {
    if (source == kRmge01DspZeroQueueShortReentrySourcePc &&
        target == kRmge01DspZeroQueueShortReentryTargetPc) {
        if (context.pending_idle_backedges != nullptr) {
            auto& counter =
                context.pending_idle_backedges->zero_queue_short_reentries;
            if (counter == std::numeric_limits<std::uint64_t>::max()) {
                std::abort();
            }
            ++counter;
        }
        return DspRmge01IdleBackEdgeKind::None;
    }
    const auto kind = dsp_classify_rmge01_idle_backedge(source, target);
    if (kind == DspRmge01IdleBackEdgeKind::None ||
        context.pending_idle_backedges == nullptr) {
        return kind;
    }
    std::uint64_t* counter = nullptr;
    switch (kind) {
    case DspRmge01IdleBackEdgeKind::ZeroQueue:
        counter = &context.pending_idle_backedges->zero_queue;
        break;
    case DspRmge01IdleBackEdgeKind::CommandWait:
        counter = &context.pending_idle_backedges->command_wait;
        break;
    case DspRmge01IdleBackEdgeKind::None:
        std::abort();
    }
    if (*counter == std::numeric_limits<std::uint64_t>::max()) {
        std::abort();
    }
    ++*counter;
    return kind;
}

inline void dsp_native_telemetry_publish_pending_worker_counts(
    DspContext& context) {
    auto* const pending_entries = context.last_retired_pc.pending_entries;
    auto* const pending_backedges = context.pending_idle_backedges;
    const auto instruction_entries =
        pending_entries != nullptr ? *pending_entries : 0u;
    const auto zero_queue =
        pending_backedges != nullptr ? pending_backedges->zero_queue : 0u;
    const auto command_wait =
        pending_backedges != nullptr ? pending_backedges->command_wait : 0u;
    const auto zero_queue_short_reentries =
        pending_backedges != nullptr
            ? pending_backedges->zero_queue_short_reentries
            : 0u;
    const auto zero_queue_complete_sequences =
        pending_backedges != nullptr
            ? pending_backedges->zero_queue_complete_sequences
            : 0u;
    const auto command_wait_complete_sequences =
        pending_backedges != nullptr
            ? pending_backedges->command_wait_complete_sequences
            : 0u;
    if (instruction_entries == 0u && zero_queue == 0u && command_wait == 0u &&
        zero_queue_short_reentries == 0u &&
        zero_queue_complete_sequences == 0u &&
        command_wait_complete_sequences == 0u) {
        return;
    }
    if (pending_entries != nullptr) {
        *pending_entries = 0u;
    }
    if (pending_backedges != nullptr) {
        pending_backedges->zero_queue = 0u;
        pending_backedges->command_wait = 0u;
        pending_backedges->zero_queue_short_reentries = 0u;
        pending_backedges->zero_queue_complete_sequences = 0u;
        pending_backedges->command_wait_complete_sequences = 0u;
    }
    if (context.host_telemetry == nullptr) {
        return;
    }

    auto& sequence = context.host_telemetry->worker_publication_sequence;
    auto current = sequence.load(std::memory_order_acquire);
    if ((current & 1u) != 0u ||
        current > std::numeric_limits<std::uint64_t>::max() - 2u ||
        !sequence.compare_exchange_strong(
            current,
            current + 1u,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        std::abort();
    }
    dsp_native_telemetry_add(
        context.host_telemetry->retired_instructions, instruction_entries);
    dsp_native_telemetry_add(
        context.host_telemetry->zero_queue_idle_backedges, zero_queue);
    dsp_native_telemetry_add(
        context.host_telemetry->command_wait_idle_backedges, command_wait);
    dsp_native_telemetry_add(
        context.host_telemetry->zero_queue_short_reentries,
        zero_queue_short_reentries);
    dsp_native_telemetry_add(
        context.host_telemetry->zero_queue_complete_sequences,
        zero_queue_complete_sequences);
    dsp_native_telemetry_add(
        context.host_telemetry->command_wait_complete_sequences,
        command_wait_complete_sequences);
    sequence.store(current + 2u, std::memory_order_release);
}

[[noreturn]] inline void dsp_hard_trap(
    DspContext& context,
    std::uint16_t pc,
    const char* reason) {
    context.pc = pc;
    throw DspHardTrap{pc, reason, context.last_retired_pc};
}

[[noreturn]] inline void dsp_hard_trap_at_current(
    const DspContext& context,
    const char* reason) {
    throw DspHardTrap{context.pc, reason, context.last_retired_pc};
}

inline std::uint16_t dsp_iram_read(const DspContext& context, std::uint16_t address) {
    if (address <= 0x0fffu) {
        return context.iram[static_cast<std::size_t>(address & 0x0fffu)];
    }
    if ((address & 0xf000u) != 0x8000u) {
        // Instruction-memory data reads decode only IRAM and IROM regions.
        // Unmapped regions do not alias the IROM's low address bits.
        return 0u;
    }
    if (!context.irom_loaded) {
        dsp_hard_trap_at_current(context, "DSP IROM read before IROM initialization");
    }
    return context.irom[static_cast<std::size_t>(address & 0x0fffu)];
}

inline std::uint16_t dsp_dram_read(const DspContext& context, std::uint16_t address) {
    const auto index = static_cast<std::size_t>(address);
    if (index >= kDspDramWords) {
        dsp_hard_trap_at_current(context, "DSP DRAM read outside memory");
    }
    return context.dram[index];
}

inline void dsp_dram_write(DspContext& context, std::uint16_t address, std::uint16_t value) {
    const auto index = static_cast<std::size_t>(address);
    if (index >= kDspDramWords) {
        dsp_hard_trap_at_current(context, "DSP DRAM write outside memory");
    }
    context.dram[index] = value;
    dsp_audio_causal_record_channel_selection_write(
        context.audio_causal_recorder, context.pc, address, value);
    dsp_audio_causal_record_mixer_output_write(
        context.audio_causal_recorder, context.pc, address, value);
}

inline std::uint32_t& dsp_dsp_mailbox(DspContext& context) {
    return context.dsp_mailbox;
}

inline const std::uint32_t& dsp_dsp_mailbox(const DspContext& context) {
    return context.dsp_mailbox;
}

inline std::uint32_t& dsp_cpu_mailbox(DspContext& context) {
    return context.cpu_mailbox;
}

inline const std::uint32_t& dsp_cpu_mailbox(const DspContext& context) {
    return context.cpu_mailbox;
}

// The two mailbox words are the only DspContext state touched from both the
// CPU thread and a free-running DSP worker thread, so every access goes
// through std::atomic_ref. The busy bit (kDspMailboxBusy) is the hardware
// "mail present" handshake: a low-half write sets it with release semantics so
// the high half it published becomes visible, and the consuming low-half read
// clears it. Reads use acquire so the spinning DSP observes the CPU's writes.
// Value semantics are byte-for-byte identical to the previous scalar version.
inline std::uint16_t dsp_mailbox_read_high(const std::uint32_t& mailbox) {
    std::atomic_ref<std::uint32_t> ref(const_cast<std::uint32_t&>(mailbox));
    return static_cast<std::uint16_t>(ref.load(std::memory_order_acquire) >> 16u);
}

inline std::uint16_t dsp_mailbox_read_low(
    std::uint32_t& mailbox,
    bool* consumed_mail = nullptr) {
    std::atomic_ref<std::uint32_t> ref(mailbox);
    const auto value = ref.fetch_and(~kDspMailboxBusy, std::memory_order_acq_rel);
    if (consumed_mail != nullptr) {
        *consumed_mail = (value & kDspMailboxBusy) != 0u;
    }
    return static_cast<std::uint16_t>(value);
}

inline std::uint16_t dsp_mailbox_peek_low(const std::uint32_t& mailbox) {
    std::atomic_ref<std::uint32_t> ref(const_cast<std::uint32_t&>(mailbox));
    return static_cast<std::uint16_t>(ref.load(std::memory_order_acquire));
}

inline void dsp_mailbox_write_high(std::uint32_t& mailbox, std::uint16_t value) {
    std::atomic_ref<std::uint32_t> ref(mailbox);
    std::uint32_t expected = ref.load(std::memory_order_relaxed);
    std::uint32_t desired;
    do {
        desired = ((static_cast<std::uint32_t>(value) << 16u) |
                   (expected & 0x0000ffffu)) &
                  ~kDspMailboxBusy;
    } while (!ref.compare_exchange_weak(
        expected, desired, std::memory_order_release, std::memory_order_relaxed));
}

[[nodiscard]] inline std::uint32_t dsp_mailbox_write_low(
    std::uint32_t& mailbox,
    std::uint16_t value) {
    std::atomic_ref<std::uint32_t> ref(mailbox);
    std::uint32_t expected = ref.load(std::memory_order_relaxed);
    std::uint32_t desired;
    do {
        desired = (expected & 0xffff0000u) | static_cast<std::uint32_t>(value) |
                  kDspMailboxBusy;
    } while (!ref.compare_exchange_weak(
        expected, desired, std::memory_order_release, std::memory_order_relaxed));
    return desired;
}

inline std::uint32_t dsp_mailbox_raw(const std::uint32_t& mailbox) {
    std::atomic_ref<std::uint32_t> ref(const_cast<std::uint32_t&>(mailbox));
    return ref.load(std::memory_order_acquire);
}

[[nodiscard]] inline const char* dsp_diagnostic_boundary_name(
    DspDiagnosticBoundary boundary) noexcept {
    switch (boundary) {
    case DspDiagnosticBoundary::None:
        return "none";
    case DspDiagnosticBoundary::DspMailboxLow:
        return "dmbl";
    case DspDiagnosticBoundary::DspInterruptRequest:
        return "dirq";
    case DspDiagnosticBoundary::HaltReturn:
        return "halt-return";
    case DspDiagnosticBoundary::ExternalInterruptReturn:
        return "external-interrupt-return";
    case DspDiagnosticBoundary::AbortReturn:
        return "abort-return";
    case DspDiagnosticBoundary::HardTrapReturn:
        return "hard-trap-return";
    case DspDiagnosticBoundary::UnexpectedReturn:
        return "unexpected-return";
    case DspDiagnosticBoundary::SingleShotReturn:
        return "single-shot-return";
    case DspDiagnosticBoundary::QuiescentWorkerQuery:
        return "quiescent-worker-query";
    }
    return "invalid";
}

[[nodiscard]] inline DspDiagnosticSnapshot dsp_capture_diagnostic_snapshot(
    const DspContext& context,
    DspDiagnosticBoundary boundary,
    std::uint64_t publication_generation,
    std::uint64_t dsp_mail_generation,
    std::uint64_t dirq_generation,
    std::uint64_t return_generation) {
    DspDiagnosticSnapshot snapshot{};
    snapshot.publication_generation = publication_generation;
    snapshot.dsp_mail_generation = dsp_mail_generation;
    snapshot.dirq_generation = dirq_generation;
    snapshot.return_generation = return_generation;
    snapshot.boundary = boundary;
    snapshot.pc = context.pc;
    snapshot.sr = context.sr;
    snapshot.st = context.st;
    snapshot.halted = context.halted;
    snapshot.dsp_mailbox = dsp_mailbox_raw(dsp_dsp_mailbox(context));
    snapshot.cpu_mailbox = dsp_mailbox_raw(dsp_cpu_mailbox(context));

    const auto copy_dram_range =
        [&context](std::uint16_t first, auto& destination) {
            for (std::size_t index = 0; index < destination.size(); ++index) {
                destination[index] = context.dram[
                    static_cast<std::size_t>(first) + index];
            }
        };
    copy_dram_range(kDspDiagnosticLowDramStart, snapshot.low_dram);
    copy_dram_range(kDspDiagnosticCommandRingStart, snapshot.command_ring);
    copy_dram_range(kDspDiagnosticWorkScratchStart, snapshot.work_scratch);
    copy_dram_range(kDspDiagnosticReleaseSlotsStart, snapshot.release_slots);
    return snapshot;
}

inline void dsp_publish_diagnostic_snapshot_locked(
    DspContext& context,
    DspDiagnosticBoundary boundary) {
    auto& state = *context.host_diagnostics;
    const auto increment = [](std::uint64_t& value) {
        if (value == std::numeric_limits<std::uint64_t>::max()) {
            std::abort();
        }
        ++value;
    };
    increment(state.publication_generation);
    switch (boundary) {
    case DspDiagnosticBoundary::DspMailboxLow:
        increment(state.dsp_mail_generation);
        break;
    case DspDiagnosticBoundary::DspInterruptRequest:
        increment(state.dirq_generation);
        break;
    case DspDiagnosticBoundary::HaltReturn:
    case DspDiagnosticBoundary::ExternalInterruptReturn:
    case DspDiagnosticBoundary::AbortReturn:
    case DspDiagnosticBoundary::HardTrapReturn:
    case DspDiagnosticBoundary::UnexpectedReturn:
    case DspDiagnosticBoundary::SingleShotReturn:
        increment(state.return_generation);
        break;
    case DspDiagnosticBoundary::None:
    case DspDiagnosticBoundary::QuiescentWorkerQuery:
        std::abort();
    }
    state.latest = dsp_capture_diagnostic_snapshot(
        context,
        boundary,
        state.publication_generation,
        state.dsp_mail_generation,
        state.dirq_generation,
        state.return_generation);
    state.valid = true;
}

inline void dsp_publish_diagnostic_snapshot(
    DspContext& context,
    DspDiagnosticBoundary boundary) {
    if (context.host_diagnostics == nullptr) {
        return;
    }
    const std::lock_guard lock(context.host_diagnostics->mutex);
    dsp_publish_diagnostic_snapshot_locked(context, boundary);
}

inline void dsp_mailbox_write_low_and_publish_diagnostic(
    DspContext& context,
    std::uint16_t value) {
    if (context.host_diagnostics == nullptr) {
        (void)dsp_mailbox_write_low(dsp_dsp_mailbox(context), value);
        return;
    }
    // Hold the publication mutex across the architectural release. A CPU that
    // observes the new busy mailbox and then asks for latest cannot overtake
    // construction of the matching coherent snapshot.
    const std::lock_guard lock(context.host_diagnostics->mutex);
    const std::uint32_t published_mailbox =
        dsp_mailbox_write_low(dsp_dsp_mailbox(context), value);
    dsp_publish_diagnostic_snapshot_locked(
        context, DspDiagnosticBoundary::DspMailboxLow);
    // The CPU can consume the atomic mailbox immediately after the release,
    // including while the bounded DRAM copy above is still in progress. Keep
    // the exact word released by this DMBL generation as its provenance rather
    // than replacing it with a later, already-consumed atomic observation.
    context.host_diagnostics->latest.dsp_mailbox = published_mailbox;
}

inline bool dsp_trace_ifx_enabled() {
    static const bool enabled = [] {
#ifdef _MSC_VER
        char value[8]{};
        std::size_t required_size = 0;
        if (getenv_s(
                &required_size,
                value,
                sizeof(value),
                "GALAXY_TRACE_DSP_IFX") != 0) {
            return false;
        }
        return required_size > 1u && value[0] != '\0' && value[0] != '0';
#else
        const char* value = std::getenv("GALAXY_TRACE_DSP_IFX");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
    }();
    return enabled;
}

inline bool dsp_trace_mem_summary_enabled() {
    static const bool enabled = [] {
#ifdef _MSC_VER
        char value[8]{};
        std::size_t required_size = 0;
        if (getenv_s(
                &required_size,
                value,
                sizeof(value),
                "GALAXY_TRACE_DSP_MEM_SUMMARY") != 0) {
            return false;
        }
        return required_size > 1u && value[0] != '\0' && value[0] != '0';
#else
        const char* value = std::getenv("GALAXY_TRACE_DSP_MEM_SUMMARY");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
    }();
    return enabled;
}

// Per-DMA nonzero-byte trace. Kept on its own flag: it fires on every nonzero
// transfer (thousands/sec once FX_BUF is live) and floods the console, which
// stalls the runtime enough to skew boot/audio timing. The cheaper rate-limited
// probes below stay on GALAXY_TRACE_DSP_MEM_SUMMARY.
inline bool dsp_trace_dma_bytes_enabled() {
    static const bool enabled = [] {
#ifdef _MSC_VER
        char value[8]{};
        std::size_t required_size = 0;
        if (getenv_s(
                &required_size,
                value,
                sizeof(value),
                "GALAXY_TRACE_DSP_DMA_BYTES") != 0) {
            return false;
        }
        return required_size > 1u && value[0] != '\0' && value[0] != '0';
#else
        const char* value = std::getenv("GALAXY_TRACE_DSP_DMA_BYTES");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
    }();
    return enabled;
}

inline bool dsp_trace_ifx_sample(
    std::atomic<std::uint64_t>& counter,
    std::uint64_t first_count,
    std::uint64_t period) {
    // Every caller uses the result only to decide whether to call
    // dsp_trace_ifx_mail(), which itself returns immediately unless
    // GALAXY_TRACE_DSP_IFX is set — but the callers reach this helper on
    // *every* DMBH/CMBH/DMBL/CMBL register read, before that check. The
    // fetch_add below is a locked read-modify-write (lock xadd), so with
    // tracing off the DSP thread was paying 20-40 cycles and a contended
    // cache line per mailbox read to feed counters whose values are never
    // observed. dsp_trace_ifx_enabled() is a static-singleton env read
    // (evaluated once, then a constant), so this early-out is free.
    //
    // Observed behaviour with tracing ON is bit-identical: the counters, the
    // sampling decisions and the log lines are all unchanged. With tracing
    // OFF the counters no longer advance, and nothing reads them on that path.
    if (!dsp_trace_ifx_enabled()) {
        return false;
    }
    const std::uint64_t count =
        counter.fetch_add(1u, std::memory_order_relaxed) + 1u;
    return count <= first_count || (period != 0u && (count % period) == 0u);
}

inline std::uint64_t dsp_trace_ifx_next_count(
    std::atomic<std::uint64_t>& counter) {
    return counter.fetch_add(1u, std::memory_order_relaxed) + 1u;
}

inline bool dsp_trace_ifx_key_mail_pc(const DspContext& context) {
    switch (context.pc) {
    case 0x06F5u:  // DSP external IRQ first CPU-mail high read
    case 0x06FBu:  // DSP external IRQ CPU-mail low consume
    case 0x0700u:  // DSP external IRQ follow-up CPU-mail high poll
    case 0x0713u:  // DSP external IRQ follow-up CPU-mail low consume
    case 0x073Fu:  // task dispatcher CPU-mail low consume
    case 0x078Fu:  // task dispatcher CPU-mail high poll
    case 0x0795u:  // task dispatcher alternate CPU-mail high poll
    case 0x07ADu:  // DSP waits for its first FromDSP mailbox to become free
    case 0x07B3u:  // DSP waits for its FromDSP mailbox to become free
    case 0x07CCu:  // main task loop CPU-mail high poll
    case 0x07D2u:  // main task loop CPU-mail low consume
        return true;
    default:
        return false;
    }
}

inline void dsp_trace_ifx_mail(
    const DspContext& context,
    const char* op,
    std::uint16_t value) {
    if (!dsp_trace_ifx_enabled()) {
        return;
    }
    std::fprintf(
        stderr,
        "[dsp-ifx] %s pc=0x%04X sr=0x%04X value=0x%04X cpu=0x%08X dsp=0x%08X\n",
        op,
        static_cast<unsigned>(context.pc),
        static_cast<unsigned>(context.sr),
        static_cast<unsigned>(value),
        static_cast<unsigned>(dsp_mailbox_raw(dsp_cpu_mailbox(context))),
        static_cast<unsigned>(dsp_mailbox_raw(dsp_dsp_mailbox(context))));
}

inline void dsp_trace_task_mail_context(const DspContext& context) {
    if (!dsp_trace_ifx_enabled()) {
        return;
    }
    const auto mail = dsp_mailbox_raw(dsp_dsp_mailbox(context));
    if (mail != 0xDCD10002u && mail != 0xDCD10005u) {
        return;
    }
    std::fprintf(
        stderr,
        "[dsp-task-mail] mail=0x%08X pc=0x%04X sr=0x%04X "
        "ar=(0x%04X,0x%04X,0x%04X,0x%04X) "
        "wr=(0x%04X,0x%04X,0x%04X,0x%04X) "
        "ix=(0x%04X,0x%04X,0x%04X,0x%04X) "
        "ac=(%lld,%lld) "
        "st=(0x%04X,0x%04X,0x%04X,0x%04X) "
        "dram350=0x%04X dram351=0x%04X dram352=0x%04X "
        "dram3A3=0x%04X dram3F9=0x%04X dram3FA=0x%04X "
        "dram3FD=0x%04X dram3FE=0x%04X dram3FF=0x%04X "
        "cpu=0x%08X dsp=0x%08X\n",
        static_cast<unsigned>(mail),
        static_cast<unsigned>(context.pc),
        static_cast<unsigned>(context.sr),
        static_cast<unsigned>(context.ar[0]),
        static_cast<unsigned>(context.ar[1]),
        static_cast<unsigned>(context.ar[2]),
        static_cast<unsigned>(context.ar[3]),
        static_cast<unsigned>(context.wr[0]),
        static_cast<unsigned>(context.wr[1]),
        static_cast<unsigned>(context.wr[2]),
        static_cast<unsigned>(context.wr[3]),
        static_cast<unsigned>(static_cast<std::uint16_t>(context.ix[0])),
        static_cast<unsigned>(static_cast<std::uint16_t>(context.ix[1])),
        static_cast<unsigned>(static_cast<std::uint16_t>(context.ix[2])),
        static_cast<unsigned>(static_cast<std::uint16_t>(context.ix[3])),
        static_cast<long long>(context.ac[0]),
        static_cast<long long>(context.ac[1]),
        static_cast<unsigned>(context.st[0]),
        static_cast<unsigned>(context.st[1]),
        static_cast<unsigned>(context.st[2]),
        static_cast<unsigned>(context.st[3]),
        static_cast<unsigned>(context.dram[0x0350u]),
        static_cast<unsigned>(context.dram[0x0351u]),
        static_cast<unsigned>(context.dram[0x0352u]),
        static_cast<unsigned>(context.dram[0x03A3u]),
        static_cast<unsigned>(context.dram[0x03F9u]),
        static_cast<unsigned>(context.dram[0x03FAu]),
        static_cast<unsigned>(context.dram[0x03FDu]),
        static_cast<unsigned>(context.dram[0x03FEu]),
        static_cast<unsigned>(context.dram[0x03FFu]),
        static_cast<unsigned>(dsp_mailbox_raw(dsp_cpu_mailbox(context))),
        static_cast<unsigned>(mail));
}

// Begins a new exact logical-depth epoch. This is deliberately named as a
// trusted-reset-owner API: no snapshot of wrapping hardware cursors can prove a
// reset (32 untracked pushes alias the zero cursor). The trusted caller must
// invoke it immediately after constructing/resetting DspContext. Raw zero
// cursors are only a defense-in-depth guard and are never used to infer depth.
[[nodiscard]] inline bool
dsp_logical_stack_depth_begin_trusted_reset_owner_epoch(
    DspContext& context,
    DspLogicalStackDepthTracker& tracker) noexcept {
    if (context.logical_stack_depth_tracker != nullptr) {
        return false;
    }
    if (tracker.owner != nullptr && tracker.owner != &context) {
        return false;
    }
    for (const auto pointer : context.stack_pointers) {
        if (pointer != 0u) {
            return false;
        }
    }
    if (tracker.epoch == std::numeric_limits<std::uint64_t>::max()) {
        tracker.owner = nullptr;
        tracker.depths.fill(0u);
        tracker.availability.fill(
            DspLogicalStackDepthAvailability::Unavailable);
        return false;
    }
    tracker.owner = &context;
    ++tracker.epoch;
    tracker.depths.fill(0u);
    tracker.availability.fill(DspLogicalStackDepthAvailability::Exact);
    context.logical_stack_depth_tracker = &tracker;
    return true;
}

[[nodiscard]] inline bool dsp_logical_stack_depths_are_exact(
    const DspContext& context) noexcept {
    const auto* tracker = context.logical_stack_depth_tracker;
    if (tracker == nullptr || tracker->owner != &context ||
        tracker->epoch == 0u) {
        return false;
    }
    for (std::size_t index = 0u; index < kDspStackRegisterCount; ++index) {
        if (tracker->availability[index] !=
                DspLogicalStackDepthAvailability::Exact ||
            static_cast<std::uint8_t>(
                tracker->depths[index] & kDspStackMask) !=
                context.stack_pointers[index]) {
            return false;
        }
    }
    return true;
}

inline void dsp_logical_stack_depth_mark_all_unavailable(
    DspLogicalStackDepthTracker& tracker) noexcept {
    tracker.availability.fill(
        DspLogicalStackDepthAvailability::Unavailable);
}

inline void dsp_logical_stack_depth_note_push(
    DspContext& context,
    std::size_t stack) noexcept {
    auto* tracker = context.logical_stack_depth_tracker;
    if (tracker == nullptr) {
        return;
    }
    if (tracker->owner != &context || tracker->epoch == 0u) {
        dsp_logical_stack_depth_mark_all_unavailable(*tracker);
        return;
    }
    if (tracker->availability[stack] !=
        DspLogicalStackDepthAvailability::Exact) {
        return;
    }
    auto& depth = tracker->depths[stack];
    if (depth == std::numeric_limits<std::uint8_t>::max()) {
        tracker->availability[stack] =
            DspLogicalStackDepthAvailability::Unavailable;
        return;
    }
    ++depth;
    if (static_cast<std::uint8_t>(depth & kDspStackMask) !=
        context.stack_pointers[stack]) {
        tracker->availability[stack] =
            DspLogicalStackDepthAvailability::Unavailable;
    }
}

inline void dsp_logical_stack_depth_note_pop(
    DspContext& context,
    std::size_t stack) noexcept {
    auto* tracker = context.logical_stack_depth_tracker;
    if (tracker == nullptr) {
        return;
    }
    if (tracker->owner != &context || tracker->epoch == 0u) {
        dsp_logical_stack_depth_mark_all_unavailable(*tracker);
        return;
    }
    if (tracker->availability[stack] !=
        DspLogicalStackDepthAvailability::Exact) {
        return;
    }
    auto& depth = tracker->depths[stack];
    if (depth == 0u) {
        tracker->availability[stack] =
            DspLogicalStackDepthAvailability::Unavailable;
        return;
    }
    --depth;
    if (static_cast<std::uint8_t>(depth & kDspStackMask) !=
        context.stack_pointers[stack]) {
        tracker->availability[stack] =
            DspLogicalStackDepthAvailability::Unavailable;
    }
}

inline void dsp_stack_push(DspContext& context, std::size_t stack, std::uint16_t value) {
    if (stack >= kDspStackRegisterCount) {
        dsp_hard_trap_at_current(context, "DSP stack push register index outside stack file");
    }

    auto& pointer = context.stack_pointers[stack];
    pointer = static_cast<std::uint8_t>((pointer + 1u) & kDspStackMask);
    context.stack[stack][pointer] = context.st[stack];
    context.st[stack] = value;
    dsp_logical_stack_depth_note_push(context, stack);
}

inline std::uint16_t dsp_stack_pop(DspContext& context, std::size_t stack) {
    if (stack >= kDspStackRegisterCount) {
        dsp_hard_trap_at_current(context, "DSP stack pop register index outside stack file");
    }

    const auto value = context.st[stack];
    auto& pointer = context.stack_pointers[stack];
    context.st[stack] = context.stack[stack][pointer];
    pointer = static_cast<std::uint8_t>((pointer - 1u) & kDspStackMask);
    dsp_logical_stack_depth_note_pop(context, stack);
    return value;
}

inline bool dsp_enter_external_interrupt(DspContext& context, std::uint16_t vector) {
    if ((context.sr & kDspSrExtIntEnable) == 0u) {
        return false;
    }
    // HALT leaves the visible DSP PC on the HALT instruction. Exception entry
    // stacks that visible PC exactly; the interrupt handler decides whether to
    // return, jump elsewhere, or remain halted.
    dsp_stack_push(context, 0, context.pc);
    dsp_stack_push(context, 1, context.sr);
    context.sr = static_cast<std::uint16_t>(context.sr & ~kDspSrExtIntEnable);
    context.pc = vector;
    context.halted = true;
    return true;
}

inline void dsp_throw_if_host_abort_requested(DspContext& context) {
    if (context.host_abort != nullptr &&
        context.host_abort->load(std::memory_order_acquire)) {
        throw DspExecutionAborted{};
    }
}

inline void dsp_accept_pending_external_interrupt(DspContext& context) {
    if (context.host_external_interrupt == nullptr ||
        (context.sr & kDspSrExtIntEnable) == 0u) {
        return;
    }

    const auto vector = context.host_external_interrupt->exchange(
        kDspNoPendingExternalInterrupt,
        std::memory_order_acq_rel);
    if (vector == kDspNoPendingExternalInterrupt) {
        return;
    }
    if (dsp_enter_external_interrupt(context, vector)) {
        throw DspExternalInterruptRequested{};
    }

    context.host_external_interrupt->store(vector, std::memory_order_release);
}

[[nodiscard]] inline bool dsp_rmge01_idle_backedge_fixed_point(
    const DspContext& context,
    DspRmge01IdleBackEdgeKind kind,
    std::uint16_t source,
    std::uint16_t target) {
    if (context.pc != target ||
        static_cast<std::uint16_t>(context.last_retired_pc) != source) {
        return false;
    }
    switch (kind) {
    case DspRmge01IdleBackEdgeKind::ZeroQueue:
        return context.dram[0x0352u] == 0u &&
               context.ar[0] == context.dram[0x0351u] &&
               context.wr[0] == 0xFFFFu && context.ax[0][1] == 0u &&
               context.sr ==
                   dsp_status_16(0, false, false, false, context.sr);
    case DspRmge01IdleBackEdgeKind::CommandWait:
        {
            const auto clear_ac0 =
                dsp_accumulator_set_value(0, context.sr);
            const auto clear_ac1 =
                dsp_accumulator_set_value(0, clear_ac0.status);
            const auto expected_ac1 = dsp_write_accumulator_mid(
                clear_ac1.value, context.dram[0x0354u], clear_ac1.status);
            const auto expected_ac0 = dsp_write_accumulator_mid(
                clear_ac0.value, context.dram[0x034Eu], clear_ac1.status);
            const auto comparison = dsp_accumulator_subtract(
                expected_ac0, expected_ac1, clear_ac1.status);
            return context.ac[0] == expected_ac0 &&
                   context.ac[1] == expected_ac1 &&
                   context.sr == comparison.status &&
                   dsp_condition_holds(DspCondition::LessOrEqual, context.sr);
        }
    case DspRmge01IdleBackEdgeKind::None:
        return false;
    }
    return false;
}

// Event-park only the two exact RMGE01 direct edges whose complete SCCs are
// statically certified above. The generated branch has already selected and
// retired the taken edge, so ctx.pc names the architectural resume target.
// A failed fixed-point certificate preserves ordinary generated execution.
inline void dsp_native_handle_static_backedge(
    DspContext& context,
    std::uint16_t source,
    std::uint16_t target) {
    const auto kind =
        dsp_native_record_static_backedge(context, source, target);
    const bool fixed_point =
        kind != DspRmge01IdleBackEdgeKind::None &&
        dsp_rmge01_idle_backedge_fixed_point(
            context, kind, source, target);

    auto* const sequence_tracker =
        context.last_retired_pc.idle_sequence_tracker;
    if (sequence_tracker != nullptr) {
        const bool complete_sequence =
            fixed_point &&
            dsp_rmge01_idle_sequence_ready(*sequence_tracker, kind);
        if (complete_sequence && context.pending_idle_backedges != nullptr) {
            std::uint64_t* counter = nullptr;
            switch (kind) {
            case DspRmge01IdleBackEdgeKind::ZeroQueue:
                counter = &context.pending_idle_backedges
                               ->zero_queue_complete_sequences;
                break;
            case DspRmge01IdleBackEdgeKind::CommandWait:
                counter = &context.pending_idle_backedges
                               ->command_wait_complete_sequences;
                break;
            case DspRmge01IdleBackEdgeKind::None:
                std::abort();
            }
            if (*counter == std::numeric_limits<std::uint64_t>::max()) {
                std::abort();
            }
            ++*counter;
        }
        // Every generated static-edge hook consumes the marker provenance.
        // A duplicate hook cannot re-credit an old traversal, and the 0824
        // short re-entry cannot seed a suffix of the eight-marker sequence.
        dsp_rmge01_idle_sequence_reset(sequence_tracker);
    }
    if (kind == DspRmge01IdleBackEdgeKind::None ||
        context.host_wake == nullptr || context.host_abort == nullptr ||
        context.host_external_interrupt == nullptr ||
        (context.sr & kDspSrExtIntEnable) == 0u ||
        !fixed_point) {
        return;
    }

    for (;;) {
        // Snapshot first, then recheck every progress source. If abort or
        // PIINT is published after these checks, its release generation bump
        // makes atomic::wait return immediately instead of losing the wake.
        const auto generation =
            context.host_wake->generation.load(std::memory_order_acquire);
        dsp_throw_if_host_abort_requested(context);
        dsp_accept_pending_external_interrupt(context);
        if (!dsp_rmge01_idle_backedge_fixed_point(
                context, kind, source, target)) {
            return;
        }

        dsp_native_telemetry_publish_pending_worker_counts(context);
        dsp_native_telemetry_add(context.host_wake->idle_wait_count);
        if (context.host_telemetry != nullptr) {
            switch (kind) {
            case DspRmge01IdleBackEdgeKind::ZeroQueue:
                dsp_native_telemetry_add(
                    context.host_telemetry->zero_queue_idle_waits);
                break;
            case DspRmge01IdleBackEdgeKind::CommandWait:
                dsp_native_telemetry_add(
                    context.host_telemetry->command_wait_idle_waits);
                break;
            case DspRmge01IdleBackEdgeKind::None:
                std::abort();
            }
        }
        context.host_wake->generation.wait(
            generation, std::memory_order_acquire);
    }
}

inline bool dsp_u32_span_is_valid(
    std::uint32_t address,
    std::uint32_t size) {
    return size == 0u ||
           static_cast<std::uint64_t>(address) + size <=
               (UINT64_C(1) << 32u);
}

inline void dsp_external_validate_span(
    DspContext& context,
    std::uint32_t address,
    std::uint32_t size) {
    if (!dsp_u32_span_is_valid(address, size)) {
        dsp_hard_trap_at_current(context, "DSP DMA MRAM span wraps the address space");
    }
    if (size == 0u) {
        return;
    }
    if (context.hardware.validate_external_span == nullptr) {
        dsp_hard_trap_at_current(context, "DSP DMA without host MRAM span validator");
    }
    if (!context.hardware.validate_external_span(
            context.hardware_user, address, size)) {
        dsp_hard_trap_at_current(context, "DSP DMA MRAM span rejected by host");
    }
}

inline std::uint8_t dsp_external_read_byte(DspContext& context, std::uint32_t address) {
    if (context.hardware.external_read_byte == nullptr) {
        dsp_hard_trap_at_current(context, "DSP external read without host memory bridge");
    }
    std::uint8_t value{};
    if (!context.hardware.external_read_byte(
            context.hardware_user, address, &value)) {
        dsp_hard_trap_at_current(context, "DSP MRAM read rejected by host");
    }
    return value;
}

inline void dsp_external_write_byte(
    DspContext& context,
    std::uint32_t address,
    std::uint8_t value) {
    if (context.hardware.external_write_byte == nullptr) {
        dsp_hard_trap_at_current(context, "DSP external write without host memory bridge");
    }
    if (!context.hardware.external_write_byte(
            context.hardware_user, address, value)) {
        dsp_hard_trap_at_current(context, "DSP MRAM write rejected by host");
    }
}

inline void dsp_external_read_span(
    DspContext& context,
    std::uint32_t address,
    std::span<std::uint8_t> destination) {
    if (destination.empty()) {
        return;
    }
    if (destination.size() > 0x4000u) {
        dsp_hard_trap_at_current(
            context, "DSP MRAM read span exceeds hardware transfer bound");
    }
    const auto size = static_cast<std::uint32_t>(destination.size());
    if (context.hardware.external_read_span != nullptr) {
        if (!context.hardware.external_read_span(
                context.hardware_user,
                address,
                destination.data(),
                size)) {
            dsp_hard_trap_at_current(
                context, "DSP MRAM span read rejected by host");
        }
        return;
    }

    // Explicit single-threaded/test fallback. The caller commits this private
    // buffer to DSP-local memory only after every byte callback succeeds, so a
    // failed fallback read still cannot partially modify IRAM/DRAM.
    for (std::uint32_t offset = 0; offset < size; ++offset) {
        destination[offset] = dsp_external_read_byte(context, address + offset);
    }
}

inline void dsp_external_write_span(
    DspContext& context,
    std::uint32_t address,
    std::span<const std::uint8_t> source) {
    if (source.empty()) {
        return;
    }
    if (source.size() > 0x4000u) {
        dsp_hard_trap_at_current(
            context, "DSP MRAM write span exceeds hardware transfer bound");
    }
    const auto size = static_cast<std::uint32_t>(source.size());
    if (context.hardware.external_write_span != nullptr) {
        if (!context.hardware.external_write_span(
                context.hardware_user,
                address,
                source.data(),
                size)) {
            dsp_hard_trap_at_current(
                context, "DSP MRAM span write rejected by host");
        }
        return;
    }

    // Explicit single-threaded/test fallback. Production hosts install the
    // span callback above because byte writes cannot provide an atomic MRAM
    // commit if a mapping disappears midway through a transfer.
    for (std::uint32_t offset = 0; offset < size; ++offset) {
        dsp_external_write_byte(context, address + offset, source[offset]);
    }
}

inline std::uint16_t& dsp_dma_memory_word(
    DspContext& context,
    bool instruction_memory,
    std::uint32_t byte_address) {
    if (instruction_memory) {
        if (byte_address >= kDspIramWords * 2u) {
            dsp_hard_trap_at_current(context, "DSP DMA instruction address outside IRAM");
        }
        return context.iram[static_cast<std::size_t>(byte_address >> 1u)];
    }

    if (byte_address >= kDspDramWords * 2u) {
        dsp_hard_trap_at_current(context, "DSP DMA data address outside DRAM");
    }
    return context.dram[static_cast<std::size_t>(byte_address >> 1u)];
}

inline std::uint8_t dsp_dma_read_byte(
    DspContext& context,
    bool instruction_memory,
    std::uint32_t byte_address) {
    const auto word = dsp_dma_memory_word(context, instruction_memory, byte_address);
    return (byte_address & 1u) == 0u ? static_cast<std::uint8_t>(word >> 8u)
                                     : static_cast<std::uint8_t>(word);
}

inline void dsp_dma_write_byte(
    DspContext& context,
    bool instruction_memory,
    std::uint32_t byte_address,
    std::uint8_t value) {
    auto& word = dsp_dma_memory_word(context, instruction_memory, byte_address);
    if ((byte_address & 1u) == 0u) {
        word = static_cast<std::uint16_t>((word & 0x00ffu) |
                                          (static_cast<std::uint16_t>(value) << 8u));
    } else {
        word = static_cast<std::uint16_t>((word & 0xff00u) | value);
    }
}

inline void dsp_run_dma(DspContext& context) {
    const auto length = context.ifx[kDspIfxDsbl];
    if (length > 0x4000u) {
        dsp_hard_trap_at_current(context, "DSP DMA length exceeds hardware transfer bound");
    }

    const auto control = context.ifx[kDspIfxDscr];
    auto host_address =
        (static_cast<std::uint32_t>(context.ifx[kDspIfxDsmah]) << 16u) |
        context.ifx[kDspIfxDsmal];
    const auto dsp_byte_address =
        static_cast<std::uint32_t>(context.ifx[kDspIfxDspa]) * 2u;
    // DIAGNOSTIC EXPERIMENT (gated): the per-frame voice-param DMA (DSP word
    // 0x3A8) reads channel_table_base + 0x6000, which lands in a zero gap; the
    // live control records sit ~0x80 higher. Bias only that transfer to test
    // whether the silence is a voice-list address offset. Hex byte count.
    if ((control & 0x0001u) == 0u && dsp_byte_address == 0x0750u) {
        static const long bias = [] {
#ifdef _MSC_VER
            char value[16]{};
            std::size_t required = 0;
            if (getenv_s(&required, value, sizeof(value),
                         "GALAXY_DSP_VOICE_DMA_BIAS") != 0 || required <= 1u) {
                return 0L;
            }
            return std::strtol(value, nullptr, 0);
#else
            const char* value = std::getenv("GALAXY_DSP_VOICE_DMA_BIAS");
            return value != nullptr ? std::strtol(value, nullptr, 0) : 0L;
#endif
        }();
        if (bias != 0L) {
            host_address = static_cast<std::uint32_t>(
                host_address + static_cast<std::uint32_t>(bias));
        }
    }
    const auto instruction_memory = (control & 0x0002u) != 0u;
    const auto to_host = (control & 0x0001u) != 0u;
    const auto amdm = context.ifx[kDspIfxAmdm];
    if (dsp_trace_mem_summary_enabled()) {
        static std::atomic<std::uint64_t> s_dma_summary_count{0};
        const std::uint64_t count = dsp_trace_ifx_next_count(s_dma_summary_count);
        if (count <= 512u || (count % 4096u) == 0u || amdm != 0u) {
            std::fprintf(
                stderr,
                "[dsp-dma-summary] #%llu pc=0x%04X sr=0x%04X control=0x%04X "
                "amdm=0x%04X len=0x%04X host=0x%08X dsp=0x%04X to-host=%u "
                "imem=%u skipped=%u\n",
                static_cast<unsigned long long>(count),
                static_cast<unsigned>(context.pc),
                static_cast<unsigned>(context.sr),
                static_cast<unsigned>(control),
                static_cast<unsigned>(amdm),
                static_cast<unsigned>(length),
                static_cast<unsigned>(host_address),
                static_cast<unsigned>(dsp_byte_address),
                to_host ? 1u : 0u,
                instruction_memory ? 1u : 0u,
                (amdm != 0u || length == 0u) ? 1u : 0u);
        }
    }
    if (dsp_trace_ifx_enabled()) {
        static std::atomic<std::uint64_t> s_dma_count{0};
        const std::uint64_t count = dsp_trace_ifx_next_count(s_dma_count);
        if (count <= 256u || (count % 4096u) == 0u) {
            std::fprintf(
                stderr,
                "[dsp-dma] #%llu pc=0x%04X control=0x%04X len=0x%04X host=0x%08X dsp=0x%04X to-host=%u imem=%u\n",
                static_cast<unsigned long long>(count),
                static_cast<unsigned>(context.pc),
                static_cast<unsigned>(control),
                static_cast<unsigned>(length),
                static_cast<unsigned>(host_address),
                static_cast<unsigned>(dsp_byte_address),
                to_host ? 1u : 0u,
                instruction_memory ? 1u : 0u);
        }
    }
    if (amdm != 0u || length == 0u) {
        dsp_channel_selection_dma_probe_record_skipped_dma_attempt(
            context.channel_selection_dma_probe);
        return;
    }
    const std::uint32_t dsp_memory_size =
        static_cast<std::uint32_t>(
            (instruction_memory ? kDspIramWords : kDspDramWords) * 2u);
    if (dsp_byte_address > dsp_memory_size ||
        length > dsp_memory_size - dsp_byte_address) {
        dsp_hard_trap_at_current(
            context,
            instruction_memory
                ? "DSP DMA span exceeds IRAM"
                : "DSP DMA span exceeds DRAM");
    }
    dsp_external_validate_span(context, host_address, length);
    // Deliberately NOT value-initialised. `transfer` below is exactly
    // [0, length) of this buffer, and both arms of the transfer write every one
    // of those bytes before anything reads them:
    //   to_host  -- the first loop writes transfer[0, length) from DSP memory,
    //               then dsp_external_write_span reads it;
    //   !to_host -- dsp_external_read_span writes transfer[0, length), then the
    //               second loop reads it.
    // The counting loop that follows also only reads [0, length). So the `{}`
    // that used to be here zeroed 0x4000 bytes that were then immediately
    // overwritten: measured DSP DMA traffic is ~14 k transfers/s with a mean
    // payload of only 166-206 B, i.e. ~229 MB/s of dead stores, and because
    // DspContext is ~30 KB the 16 KiB memset also evicts roughly half of L1D on
    // every transfer.
    std::array<std::uint8_t, 0x4000u> transfer_bytes;
    std::span<std::uint8_t> transfer{
        transfer_bytes.data(), static_cast<std::size_t>(length)};
    std::uint32_t transfer_nonzero = 0u;
    if (to_host) {
        for (std::uint32_t offset = 0; offset < length; ++offset) {
            transfer[offset] = dsp_dma_read_byte(
                context, instruction_memory, dsp_byte_address + offset);
        }
        dsp_external_write_span(context, host_address, transfer);
    } else {
        dsp_external_read_span(context, host_address, transfer);
        for (std::uint32_t offset = 0; offset < length; ++offset) {
            dsp_dma_write_byte(
                context,
                instruction_memory,
                dsp_byte_address + offset,
                transfer[offset]);
        }
    }
    for (const std::uint8_t byte : transfer) {
        if (byte != 0u) {
            ++transfer_nonzero;
        }
    }
    const std::uint64_t validated_channel_sequence_before =
        context.channel_selection_dma_probe != nullptr
            ? context.channel_selection_dma_probe->validated_channel_sequence
            : 0u;
    dsp_channel_selection_dma_probe_record_dma(
        context.channel_selection_dma_probe,
        context.pc,
        instruction_memory,
        to_host,
        host_address,
        dsp_byte_address,
        length);
    if (context.channel_selection_dma_probe != nullptr &&
        context.channel_selection_dma_probe->validated_channel_sequence !=
            validated_channel_sequence_before) {
        audio::native_dsp_audio_bus_begin_channel(
            context.audio_bus_state,
            context.channel_selection_dma_probe->last_validated_channel,
            context.audio_bus_controls);
    } else if (!instruction_memory && !to_host &&
               dsp_byte_address == 0x0800u && length == 0x0180u) {
        // Transfer shape alone is not selected-channel identity. Clear an old
        // selection rather than applying its category (or a guessed fallback)
        // to a shape-identical DMA that lacks the exact render-branch proof.
        audio::native_dsp_audio_bus_clear_selection(context.audio_bus_state);
    }
    dsp_audio_causal_record_dma(
        context.audio_causal_recorder,
        context.pc,
        control,
        instruction_memory,
        to_host,
        host_address,
        dsp_byte_address,
        length,
        transfer_nonzero,
        transfer);
    dsp_native_telemetry_publish_pending_worker_counts(context);
    if (context.host_telemetry != nullptr) {
        if (to_host) {
            dsp_native_telemetry_add(
                context.host_telemetry->dma_out_transfers);
            dsp_native_telemetry_add(
                context.host_telemetry->dma_out_bytes, length);
            dsp_native_telemetry_add(
                context.host_telemetry->dma_out_nonzero_bytes,
                transfer_nonzero);
        } else {
            dsp_native_telemetry_add(
                context.host_telemetry->dma_in_transfers);
            dsp_native_telemetry_add(
                context.host_telemetry->dma_in_bytes, length);
            dsp_native_telemetry_add(
                context.host_telemetry->dma_in_nonzero_bytes,
                transfer_nonzero);
        }
    }
    // Diagnostic-only: at render start the ucode DMAs FX_BUF (CH_BUF+0x6000 =
    // 0x807B64E0). Dump the channel table CH_BUF (0x807B04E0, 64 x 0x180) the
    // guest produced, so we can see whether the active music voices (source at
    // +0x118, ARAM offset) are present for the ucode under native mode.
    if (dsp_trace_mem_summary_enabled() && !instruction_memory && !to_host &&
        host_address == 0x807B64E0u) {
        static std::atomic<int> s_chdump{0};
        const int n = s_chdump.fetch_add(1, std::memory_order_relaxed);
        if (n < 6 || (n % 256) == 0) {
            const auto read_be32_from = [](const std::uint8_t* bytes) {
                return (static_cast<std::uint32_t>(bytes[0]) << 24u) |
                       (static_cast<std::uint32_t>(bytes[1]) << 16u) |
                       (static_cast<std::uint32_t>(bytes[2]) << 8u) |
                       static_cast<std::uint32_t>(bytes[3]);
            };
            const auto read_be16_from = [](const std::uint8_t* bytes) {
                return static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(bytes[0]) << 8u) |
                    bytes[1]);
            };
            // Resolve CH_BUF/control-pool from the guest globals rather than
            // assuming a fixed base: if JAudio never initialized under native,
            // these read 0 and the "active=0" result would be a false negative.
            std::array<std::uint8_t, 12u> globals{};
            dsp_external_read_span(context, 0x806A2BD0u, globals);
            const std::uint32_t ctrl_pool_ptr =
                read_be32_from(globals.data());
            const std::uint32_t chan_table_ptr =
                read_be32_from(globals.data() + 8u);
            const std::uint32_t chbuf =
                (chan_table_ptr != 0u) ? chan_table_ptr : 0x807B04E0u;
            constexpr std::uint32_t kChannelRecordBytes = 0x180u;
            constexpr std::uint32_t kChannelsPerBlock = 32u;
            constexpr std::uint32_t kChannelBlockBytes =
                kChannelRecordBytes * kChannelsPerBlock;
            constexpr std::uint32_t kChannelTableBytes =
                kChannelBlockBytes * 2u;
            if (!dsp_u32_span_is_valid(chbuf, kChannelTableBytes)) {
                dsp_hard_trap_at_current(
                    context, "DSP channel-table diagnostic span wraps MRAM");
            }
            std::uint32_t active = 0u;
            std::fprintf(
                stderr,
                "[dsp-chbuf] dump#%d chan-tbl=0x%08X ctrl-pool=0x%08X ",
                n, static_cast<unsigned>(chan_table_ptr),
                static_cast<unsigned>(ctrl_pool_ptr));
            std::array<std::uint8_t, kChannelBlockBytes> channel_block{};
            for (std::uint32_t block = 0u; block < 2u; ++block) {
                dsp_external_read_span(
                    context,
                    chbuf + block * kChannelBlockBytes,
                    channel_block);
                for (std::uint32_t local = 0u;
                     local < kChannelsPerBlock;
                     ++local) {
                    const std::uint32_t channel =
                        block * kChannelsPerBlock + local;
                    const std::uint8_t* const record =
                        channel_block.data() + local * kChannelRecordBytes;
                    if (read_be16_from(record) != 0u) {
                        ++active;
                        if (active <= 4u) {
                            std::fprintf(
                                stderr,
                                "ch%u(src=0x%08X cur=0x%08X cnt=%u fmt=0x%04X) ",
                                static_cast<unsigned>(channel),
                                static_cast<unsigned>(
                                    read_be32_from(record + 0x118u)),
                                static_cast<unsigned>(
                                    read_be32_from(record + 0x070u)),
                                static_cast<unsigned>(
                                    read_be32_from(record + 0x11Cu)),
                                static_cast<unsigned>(
                                    read_be16_from(record + 0x064u)));
                            // DIAGNOSTIC EXPERIMENT (gated): word 0x428 in the
                            // per-frame voice-param DMA (host=CH_BUF, dsp=0x0800,
                            // len=0x180 -- the whole-record copy, distinct from
                            // the FX_BUF+0x6000 transfer this block is nested
                            // under) maps to TChannel offset +0x50, an unnamed
                            // field even in the public SMG decompilation
                            // (Petari JASDspInterface.hpp: "u16 _50;"). The DSP
                            // ucode at pc 0x0DD4-0x0DEC sign-extends and
                            // logically shifts this field into an address --
                            // dump raw bytes 0x40-0x60 to see its actual value.
                            std::fprintf(stderr, "ch%u[0x40..0x60)=",
                                static_cast<unsigned>(channel));
                            for (std::uint32_t off = 0x40u; off < 0x60u;
                                 ++off) {
                                std::fprintf(
                                    stderr, "%02X",
                                    static_cast<unsigned>(record[off]));
                            }
                            std::fprintf(stderr, " ");
                        }
                    }
                }
            }
            std::fprintf(stderr, "| active=%u\n", static_cast<unsigned>(active));
        }
    }
    // Diagnostic-only: when a per-voice sample DMA-in from a MEM1 buffer comes
    // back all-zero, the wave data likely lives in the MEM2 VARAM/ARAM backing
    // (DsetVARAM base 0x90000800). Probe candidate base transforms to learn the
    // exact address the ucode SHOULD have read so the fix is unambiguous.
    if (dsp_trace_mem_summary_enabled() && !instruction_memory && !to_host &&
        transfer_nonzero == 0u && host_address >= 0x80800000u &&
        host_address < 0x81800000u) {
        static std::atomic<int> s_alt{0};
        if (s_alt.fetch_add(1, std::memory_order_relaxed) < 12) {
            const std::uint32_t off1 = host_address - 0x80000000u;  // MEM1 offset
            const std::uint32_t alt_a = host_address + 0x10000000u; // 0x80->0x90
            const std::uint32_t alt_b = 0x90000800u + off1;         // VARAM base
            const std::uint32_t alt_c = 0x90000000u + off1;         // MEM2 base
            std::array<std::uint8_t, 0x4000u> diagnostic_span{};
            auto count_nz = [&](std::uint32_t base) {
                std::span<std::uint8_t> bytes{
                    diagnostic_span.data(), static_cast<std::size_t>(length)};
                dsp_external_read_span(context, base, bytes);
                std::uint32_t nz = 0u;
                for (const std::uint8_t byte : bytes) {
                    if (byte != 0u) {
                        ++nz;
                    }
                }
                return nz;
            };
            std::fprintf(
                stderr,
                "[dsp-altbase] host=0x%08X len=0x%04X | +0x10000000(0x%08X)=%u "
                "varam0x90000800(0x%08X)=%u mem2base(0x%08X)=%u\n",
                static_cast<unsigned>(host_address),
                static_cast<unsigned>(length),
                static_cast<unsigned>(alt_a), static_cast<unsigned>(count_nz(alt_a)),
                static_cast<unsigned>(alt_b), static_cast<unsigned>(count_nz(alt_b)),
                static_cast<unsigned>(alt_c), static_cast<unsigned>(count_nz(alt_c)));
        }
    }
    // Diagnostic-only: when a command/parameter DMA-in from the JAudio control
    // region comes back all-zero, scan a wider guest window once to learn
    // whether the real command data lives at a nearby (mis-pointed) address or
    // is simply never written by the guest.
    if (dsp_trace_mem_summary_enabled() && !instruction_memory && !to_host &&
        transfer_nonzero == 0u && host_address >= 0x807B0000u &&
        host_address < 0x807C0000u) {
        static std::atomic<int> s_scans{0};
        if (s_scans.fetch_add(1, std::memory_order_relaxed) < 8) {
            const std::uint32_t base =
                (host_address >= 0x400u) ? (host_address - 0x400u) : 0u;
            std::array<std::uint8_t, 0xC00u> diagnostic_window{};
            dsp_external_read_span(context, base, diagnostic_window);
            std::uint32_t first_nz_addr = 0u;
            std::uint32_t last_nz_addr = 0u;
            std::uint32_t nz_total = 0u;
            for (std::uint32_t off = 0; off < 0xC00u; ++off) {
                if (diagnostic_window[off] != 0u) {
                    if (nz_total == 0u) {
                        first_nz_addr = base + off;
                    }
                    last_nz_addr = base + off;
                    ++nz_total;
                }
            }
            const std::uint32_t chtab_base =
                (static_cast<std::uint32_t>(context.dram[0x0380]) << 16u) |
                context.dram[0x0381];
            std::fprintf(
                stderr,
                "[dsp-cmd-scan] dmahost=0x%08X dspbyte=0x%04X chtab_dram=0x%08X "
                "nz_total=%u first_nz=0x%08X last_nz=0x%08X\n",
                static_cast<unsigned>(host_address),
                static_cast<unsigned>(dsp_byte_address),
                static_cast<unsigned>(chtab_base),
                static_cast<unsigned>(nz_total),
                static_cast<unsigned>(first_nz_addr),
                static_cast<unsigned>(last_nz_addr));
            // Hex window straddling the zero->data boundary the DSP misses.
            for (std::uint32_t row = 0; row < 0x100u; row += 0x10u) {
                const std::uint32_t row_addr = host_address + row;
                std::fprintf(stderr, "[dsp-cmd-hex] 0x%08X:",
                             static_cast<unsigned>(row_addr));
                for (std::uint32_t b = 0; b < 0x10u; ++b) {
                    std::fprintf(
                        stderr, " %02X",
                        static_cast<unsigned>(
                            diagnostic_window[0x400u + row + b]));
                }
                std::fprintf(stderr, "\n");
            }
        }
    }
    // Diagnostic-only: split "input samples arrived zero" from "mix zeroed them"
    // by reporting nonzero byte counts per DMA direction. Sampled to stay cheap.
    if (dsp_trace_dma_bytes_enabled() && !instruction_memory) {
        static std::atomic<std::uint64_t> s_in_count{0};
        static std::atomic<std::uint64_t> s_out_count{0};
        auto& counter = to_host ? s_out_count : s_in_count;
        const std::uint64_t n = dsp_trace_ifx_next_count(counter);
        if (n <= 64u || transfer_nonzero != 0u || (n % 16384u) == 0u) {
            std::fprintf(
                stderr,
                "[dsp-dma-bytes] dir=%s #%llu len=0x%04X nonzero=%u host=0x%08X dsp=0x%04X\n",
                to_host ? "out" : "in",
                static_cast<unsigned long long>(n),
                static_cast<unsigned>(length),
                static_cast<unsigned>(transfer_nonzero),
                static_cast<unsigned>(host_address),
                static_cast<unsigned>(dsp_byte_address));
        }
    }
}

inline std::uint32_t dsp_accelerator_pair(
    const DspContext& context,
    std::uint16_t high_register,
    std::uint16_t low_register) {
    return (static_cast<std::uint32_t>(context.ifx[high_register]) << 16u) |
           context.ifx[low_register];
}

inline void dsp_accelerator_set_pair(
    DspContext& context,
    std::uint16_t high_register,
    std::uint16_t low_register,
    std::uint32_t value,
    std::uint32_t mask) {
    const auto masked = value & mask;
    context.ifx[high_register] = static_cast<std::uint16_t>(masked >> 16u);
    context.ifx[low_register] = static_cast<std::uint16_t>(masked);
}

inline void dsp_accelerator_write_pair_high(
    DspContext& context,
    std::uint16_t high_register,
    std::uint16_t low_register,
    std::uint16_t value,
    std::uint32_t mask) {
    const auto old_value = dsp_accelerator_pair(context, high_register, low_register);
    dsp_accelerator_set_pair(
        context,
        high_register,
        low_register,
        (static_cast<std::uint32_t>(value) << 16u) | (old_value & 0x0000ffffu),
        mask);
}

inline void dsp_accelerator_write_pair_low(
    DspContext& context,
    std::uint16_t high_register,
    std::uint16_t low_register,
    std::uint16_t value,
    std::uint32_t mask) {
    const auto old_value = dsp_accelerator_pair(context, high_register, low_register);
    dsp_accelerator_set_pair(
        context,
        high_register,
        low_register,
        (old_value & 0xffff0000u) | value,
        mask);
}

inline std::int16_t dsp_ifx_s16(const DspContext& context, std::uint16_t reg) {
    return static_cast<std::int16_t>(context.ifx[reg]);
}

inline void dsp_accelerator_validate_span(
    DspContext& context,
    std::uint32_t address,
    std::uint32_t size,
    bool write) {
    if (!dsp_u32_span_is_valid(address, size)) {
        dsp_hard_trap_at_current(context, "DSP accelerator ARAM span wraps the address space");
    }
    if (size == 0u) {
        return;
    }
    const bool uses_dedicated_aram = write
        ? (context.hardware.aram_write_byte != nullptr ||
           context.hardware.aram_write_u16 != nullptr)
        : (context.hardware.aram_read_byte != nullptr ||
           context.hardware.aram_read_u16 != nullptr);
    const auto validator = uses_dedicated_aram
                               ? context.hardware.validate_aram_span
                               : context.hardware.validate_external_span;
    if (validator == nullptr) {
        dsp_hard_trap_at_current(context, "DSP accelerator without host ARAM span validator");
    }
    if (!validator(context.hardware_user, address, size)) {
        dsp_hard_trap_at_current(context, "DSP accelerator ARAM span rejected by host");
    }
}

inline std::uint8_t dsp_accelerator_read_byte(DspContext& context, std::uint32_t address) {
    // Prefer a dedicated ARAM router; fall back to the DMA/external bus when the
    // host has not installed one (single-bus callers and the bit-exact tests).
    const auto reader = context.hardware.aram_read_byte != nullptr
                            ? context.hardware.aram_read_byte
                            : context.hardware.external_read_byte;
    if (reader == nullptr) {
        dsp_hard_trap_at_current(context, "DSP accelerator read without ARAM bridge");
    }
    std::uint8_t value{};
    if (!reader(context.hardware_user, address, &value)) {
        dsp_hard_trap_at_current(context, "DSP ARAM read rejected by host");
    }
    return value;
}

inline void dsp_accelerator_write_byte(
    DspContext& context,
    std::uint32_t address,
    std::uint8_t value) {
    const auto writer = context.hardware.aram_write_byte != nullptr
                            ? context.hardware.aram_write_byte
                            : context.hardware.external_write_byte;
    if (writer == nullptr) {
        dsp_hard_trap_at_current(context, "DSP accelerator write without ARAM bridge");
    }
    if (!writer(context.hardware_user, address, value)) {
        dsp_hard_trap_at_current(context, "DSP ARAM write rejected by host");
    }
}

inline std::uint16_t dsp_accelerator_read_u16(
    DspContext& context,
    std::uint32_t address) {
    if (context.hardware.aram_read_u16 != nullptr) {
        std::uint16_t value{};
        if (!context.hardware.aram_read_u16(
                context.hardware_user, address, &value)) {
            dsp_hard_trap_at_current(
                context, "DSP ARAM word read rejected by host");
        }
        return value;
    }
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(
             dsp_accelerator_read_byte(context, address)) << 8u) |
        dsp_accelerator_read_byte(context, address + 1u));
}

inline void dsp_accelerator_write_u16(
    DspContext& context,
    std::uint32_t address,
    std::uint16_t value) {
    if (context.hardware.aram_write_u16 != nullptr) {
        if (!context.hardware.aram_write_u16(
                context.hardware_user, address, value)) {
            dsp_hard_trap_at_current(
                context, "DSP ARAM word write rejected by host");
        }
        return;
    }
    dsp_accelerator_write_byte(
        context, address, static_cast<std::uint8_t>(value >> 8u));
    dsp_accelerator_write_byte(
        context, address + 1u, static_cast<std::uint8_t>(value));
}

inline void dsp_accelerator_signal(
    DspContext& context,
    DspAcceleratorException exception) {
    if (context.hardware.accelerator_exception == nullptr) {
        dsp_hard_trap_at_current(context, "DSP accelerator exception without host callback");
    }
    context.hardware.accelerator_exception(context.hardware_user, exception);
}

inline std::uint32_t dsp_accelerator_start_address(const DspContext& context) {
    return dsp_accelerator_pair(context, kDspIfxAcsah, kDspIfxAcsal);
}

inline std::uint32_t dsp_accelerator_end_address(const DspContext& context) {
    return dsp_accelerator_pair(context, kDspIfxAceah, kDspIfxAceal);
}

inline std::uint32_t dsp_accelerator_current_address(const DspContext& context) {
    return dsp_accelerator_pair(context, kDspIfxAccah, kDspIfxAccal);
}

inline void dsp_accelerator_set_current_address(DspContext& context, std::uint32_t address) {
    dsp_accelerator_set_pair(context, kDspIfxAccah, kDspIfxAccal, address, 0xbfffffffu);
}

inline std::uint16_t dsp_accelerator_current_sample(DspContext& context) {
    const auto current_address = dsp_accelerator_current_address(context);
    switch (context.ifx[kDspIfxFormat] & 0x0003u) {
    case 0u: {
        const auto packed = dsp_accelerator_read_byte(context, current_address >> 1u);
        return (current_address & 1u) != 0u ? static_cast<std::uint16_t>(packed & 0x0fu)
                                            : static_cast<std::uint16_t>(packed >> 4u);
    }
    case 1u:
        return dsp_accelerator_read_byte(context, current_address);
    case 2u: {
        const auto byte_address = current_address * 2u;
        dsp_accelerator_validate_span(context, byte_address, 2u, false);
        return dsp_accelerator_read_u16(context, byte_address);
    }
    default:
        dsp_hard_trap_at_current(context, "DSP accelerator raw read has unsupported format");
    }
}

inline std::uint16_t dsp_accelerator_read_raw(DspContext& context) {
    const auto old_address = dsp_accelerator_current_address(context);
    const auto value = dsp_accelerator_current_sample(context);
    const auto next_address = old_address + 1u;

    if (old_address == dsp_accelerator_end_address(context)) {
        dsp_accelerator_set_current_address(context, dsp_accelerator_start_address(context));
        dsp_accelerator_signal(context, DspAcceleratorException::RawReadEnd);
    } else {
        dsp_accelerator_set_current_address(context, next_address);
    }
    return value;
}

inline void dsp_accelerator_write_raw(DspContext& context, std::uint16_t value) {
    const auto current_address = dsp_accelerator_current_address(context);
    if ((current_address & 0x80000000u) == 0u) {
        dsp_hard_trap_at_current(context, "DSP accelerator raw write outside ARAM address space");
    }

    const auto byte_address = current_address * 2u;
    dsp_accelerator_validate_span(context, byte_address, 2u, true);
    dsp_accelerator_write_u16(context, byte_address, value);
    dsp_accelerator_set_current_address(context, current_address + 1u);
    dsp_accelerator_signal(context, DspAcceleratorException::RawWriteEnd);
}

inline std::uint16_t dsp_accelerator_read_sample(DspContext& context) {
    if (context.accelerator_reads_stopped) {
        dsp_audio_causal_record_accelerator_stopped(
            context.audio_causal_recorder,
            context.pc,
            dsp_accelerator_current_address(context));
        if (dsp_trace_ifx_enabled()) {
            static std::atomic<std::uint64_t> s_stopped_reads{0};
            const std::uint64_t count = dsp_trace_ifx_next_count(s_stopped_reads);
            if (count <= 64u || (count % 4096u) == 0u) {
                std::fprintf(
                    stderr,
                    "[dsp-accel] stopped-read #%llu pc=0x%04X start=0x%08X end=0x%08X cur=0x%08X\n",
                    static_cast<unsigned long long>(count),
                    static_cast<unsigned>(context.pc),
                    static_cast<unsigned>(dsp_accelerator_start_address(context)),
                    static_cast<unsigned>(dsp_accelerator_end_address(context)),
                    static_cast<unsigned>(dsp_accelerator_current_address(context)));
            }
        }
        return 0;
    }
    const auto format = context.ifx[kDspIfxFormat];
    if ((format & 0xffc0u) != 0u) {
        dsp_hard_trap_at_current(context, "DSP accelerator sample format has unsupported high bits");
    }

    const auto decode = static_cast<std::uint16_t>((format >> 2u) & 0x0003u);
    const auto gain_scale = static_cast<std::uint16_t>((format >> 4u) & 0x0003u);
    std::int16_t raw_sample{};
    if (decode == 1u || decode == 3u) {
        raw_sample = dsp_ifx_s16(context, kDspIfxAcin);
    } else {
        raw_sample = static_cast<std::int16_t>(dsp_accelerator_current_sample(context));
    }

    const auto coefficient_index =
        static_cast<std::uint16_t>(((context.ifx[kDspIfxPredScale] >> 4u) & 0x0007u) * 2u);
    const auto coefficient_1 =
        static_cast<std::int32_t>(dsp_ifx_s16(context, kDspIfxCoefA1Base + coefficient_index));
    const auto coefficient_2 =
        static_cast<std::int32_t>(dsp_ifx_s16(context, kDspIfxCoefA1Base + coefficient_index + 1u));
    const auto yn1 = static_cast<std::int32_t>(dsp_ifx_s16(context, kDspIfxYn1));
    const auto yn2 = static_cast<std::int32_t>(dsp_ifx_s16(context, kDspIfxYn2));
    std::uint32_t current_address = dsp_accelerator_current_address(context);
    const std::uint32_t old_current_address = current_address;
    std::uint8_t step_size = 0;
    std::uint16_t value = 0;

    if (decode == 0u) {
        auto nibble = static_cast<std::int32_t>(raw_sample & 0x000f);
        if (nibble >= 8) {
            nibble -= 16;
        }
        const auto scale = static_cast<std::int32_t>(1u << (context.ifx[kDspIfxPredScale] & 0x000fu));
        // Preserve the signed-32 predictor model with defined modular
        // accumulation. Each signed-16 product fits int32, but their sum need
        // not. This does not assert a measured hardware intermediate width.
        const std::uint32_t prediction_bits = 0x400u +
            static_cast<std::uint32_t>(coefficient_1 * yn1) +
            static_cast<std::uint32_t>(coefficient_2 * yn2);
        const auto prediction = std::bit_cast<std::int32_t>(prediction_bits);
        const auto decoded = (scale * nibble) + (prediction >> 11);
        const auto clamped = std::clamp(decoded, -0x7fff, 0x7fff);
        value = static_cast<std::uint16_t>(static_cast<std::int16_t>(clamped));
        step_size = 2;
        context.ifx[kDspIfxYn2] = context.ifx[kDspIfxYn1];
        context.ifx[kDspIfxYn1] = value;
        current_address += 1u;

        const auto end_address = dsp_accelerator_end_address(context);
        const auto start_address = dsp_accelerator_start_address(context);
        if ((end_address & 0x0fu) == 0u && current_address == end_address) {
            current_address = start_address + 1u;
        } else if ((end_address & 0x0fu) == 1u && current_address == end_address - 1u) {
            current_address = start_address;
        } else if ((current_address & 0x0fu) == 0u) {
            context.ifx[kDspIfxPredScale] =
                dsp_accelerator_read_byte(context, (current_address & ~0x0fu) >> 1u);
            current_address += 2u;
            step_size = static_cast<std::uint8_t>(step_size + 2u);
        }
    } else if (decode == 1u || decode == 2u || decode == 3u) {
        std::uint8_t gain_shift = 0;
        switch (gain_scale) {
        case 0u:
            gain_shift = 11;
            break;
        case 1u:
            gain_shift = 0;
            break;
        case 2u:
            gain_shift = 16;
            break;
        default:
            dsp_hard_trap_at_current(context, "DSP accelerator sample gain mode is unsupported");
        }

        const auto gain = static_cast<std::int32_t>(dsp_ifx_s16(context, kDspIfxGain));
        const auto raw = static_cast<std::int32_t>(raw_sample);
        // The observable result is the low sixteen bits. Widening only the
        // sum after the existing per-product shifts preserves those bits and
        // avoids signed overflow without changing shift or wrap ordering.
        const std::int64_t decoded =
            std::int64_t{(gain * raw) >> gain_shift} +
            ((coefficient_1 * yn1) >> gain_shift) +
            ((coefficient_2 * yn2) >> gain_shift);
        value = static_cast<std::uint16_t>(decoded);
        context.ifx[kDspIfxYn2] = context.ifx[kDspIfxYn1];
        context.ifx[kDspIfxYn1] = value;
        step_size = 2;
        if (decode != 1u) {
            current_address += 1u;
        }
    } else {
        dsp_hard_trap_at_current(context, "DSP accelerator sample read has unsupported decode mode");
    }

    const bool sample_read_ended =
        current_address ==
        dsp_accelerator_end_address(context) + step_size - 1u;
    if (sample_read_ended) {
        current_address = dsp_accelerator_start_address(context);
        context.accelerator_reads_stopped = true;
    }

    dsp_accelerator_set_current_address(context, current_address);
    if (sample_read_ended) {
        dsp_accelerator_signal(context, DspAcceleratorException::SampleReadEnd);
    }
    value = static_cast<std::uint16_t>(
        audio::native_dsp_audio_bus_apply_sample(
            context.audio_bus_state,
            static_cast<std::int16_t>(value)));
    if (dsp_trace_ifx_enabled()) {
        static std::atomic<std::uint64_t> s_sample_reads{0};
        const std::uint64_t count = dsp_trace_ifx_next_count(s_sample_reads);
        const bool interesting =
            value != 0u || raw_sample != 0 || (count <= 256u) ||
            ((count % 4096u) == 0u);
        if (interesting) {
            std::fprintf(
                stderr,
                "[dsp-accel] sample #%llu pc=0x%04X fmt=0x%04X decode=%u gain=%u "
                "start=0x%08X end=0x%08X cur-old=0x%08X cur-new=0x%08X "
                "raw=0x%04X pred=0x%04X coef=(%d,%d) hist=(%d,%d) value=0x%04X stopped=%u\n",
                static_cast<unsigned long long>(count),
                static_cast<unsigned>(context.pc),
                static_cast<unsigned>(format),
                static_cast<unsigned>(decode),
                static_cast<unsigned>(gain_scale),
                static_cast<unsigned>(dsp_accelerator_start_address(context)),
                static_cast<unsigned>(dsp_accelerator_end_address(context)),
                static_cast<unsigned>(old_current_address),
                static_cast<unsigned>(current_address),
                static_cast<unsigned>(static_cast<std::uint16_t>(raw_sample)),
                static_cast<unsigned>(context.ifx[kDspIfxPredScale]),
                static_cast<int>(coefficient_1),
                static_cast<int>(coefficient_2),
                static_cast<int>(yn1),
                static_cast<int>(yn2),
                static_cast<unsigned>(value),
                context.accelerator_reads_stopped ? 1u : 0u);
        }
    }
    dsp_audio_causal_record_accelerator_sample(
        context.audio_causal_recorder,
        context.pc,
        format,
        old_current_address,
        raw_sample,
        value);
    return value;
}

inline std::uint16_t dsp_ifx_read(DspContext& context, std::uint16_t address) {
    const auto reg = static_cast<std::uint16_t>(address & 0x00ffu);
    switch (reg) {
    case kDspIfxDmbh:
        {
            static std::atomic<std::uint64_t> s_dmbh_reads{0};
            static std::atomic<std::uint64_t> s_dmbh_key_reads{0};
            auto trace_value = [&](std::uint16_t value) {
                if ((dsp_trace_ifx_key_mail_pc(context) &&
                     dsp_trace_ifx_sample(
                         s_dmbh_key_reads, 512u, 4096u)) ||
                    dsp_trace_ifx_sample(s_dmbh_reads, 256u, 65536u)) {
                    dsp_trace_ifx_mail(context, "read DMBH", value);
                }
            };

            for (;;) {
                dsp_throw_if_host_abort_requested(context);
                dsp_accept_pending_external_interrupt(context);
                const auto value =
                    dsp_mailbox_read_high(dsp_dsp_mailbox(context));

                // The generated RMGE01 ucode has two back-edge loops that read
                // DMBH until the CPU consumes the previous DSP->CPU mail. At
                // native x64 speed those architectural polling loops otherwise
                // retire without bound, steal a host core, and can starve both
                // the audio consumer and the guest CPU that must perform the
                // consume. Only park the classified polling sites; every other
                // DMBH read retains immediate register-read semantics, and
                // contexts without a host event remain wholly synchronous.
                if (!dsp_is_rmge01_dmbh_poll_pc(context.pc) ||
                    (value & 0x8000u) == 0u ||
                    context.host_wake == nullptr) {
                    trace_value(value);
                    return value;
                }

                // Snapshot first, then recheck every wake source and the
                // mailbox. If the CPU consumes the mail between this recheck
                // and wait(), its release increment changes generation and
                // atomic::wait returns immediately instead of losing the
                // notification. A wake for an unrelated event loops until the
                // mailbox is actually free (or abort/PIINT throws).
                const auto generation = context.host_wake->generation.load(
                    std::memory_order_acquire);
                dsp_throw_if_host_abort_requested(context);
                dsp_accept_pending_external_interrupt(context);
                const auto rechecked_value =
                    dsp_mailbox_read_high(dsp_dsp_mailbox(context));
                if ((rechecked_value & 0x8000u) == 0u) {
                    trace_value(rechecked_value);
                    return rechecked_value;
                }

                trace_value(rechecked_value);
                dsp_native_telemetry_publish_pending_worker_counts(context);
                context.host_wake->idle_wait_count.fetch_add(
                    1u, std::memory_order_release);
                context.host_wake->generation.wait(
                    generation, std::memory_order_acquire);
            }
        }
    case kDspIfxDmbl:
        dsp_throw_if_host_abort_requested(context);
        dsp_accept_pending_external_interrupt(context);
        {
            const std::uint16_t value =
                dsp_mailbox_read_low(dsp_dsp_mailbox(context));
            static std::atomic<std::uint64_t> s_dmbl_reads{0};
            static std::atomic<std::uint64_t> s_dmbl_key_reads{0};
            if ((dsp_trace_ifx_key_mail_pc(context) &&
                 dsp_trace_ifx_sample(s_dmbl_key_reads, 512u, 4096u)) ||
                dsp_trace_ifx_sample(s_dmbl_reads, 256u, 4096u)) {
                dsp_trace_ifx_mail(context, "read DMBL", value);
            }
            return value;
        }
    case kDspIfxCmbh:
        {
            static std::atomic<std::uint64_t> s_cmbh_reads{0};
            static std::atomic<std::uint64_t> s_cmbh_key_reads{0};
            auto trace_value = [&](std::uint16_t value) {
                if ((dsp_trace_ifx_key_mail_pc(context) &&
                     dsp_trace_ifx_sample(
                         s_cmbh_key_reads, 512u, 4096u)) ||
                    dsp_trace_ifx_sample(s_cmbh_reads, 256u, 65536u)) {
                    dsp_trace_ifx_mail(context, "read CMBH", value);
                }
            };

            for (;;) {
                // A free-running compiled RMGE01 task reaches CMBH while
                // waiting for the next CPU mail. Retiring any of the four
                // proven polling loops at native x64 speed is not DSP timing:
                // it burns a host core and contends on the mailbox cache line
                // without advancing any hardware-visible state. Other CMBH
                // reads retain immediate register-read semantics so the
                // generated control flow, including PC 0x06F5's empty-mail
                // branch, owns the result.
                dsp_throw_if_host_abort_requested(context);
                dsp_accept_pending_external_interrupt(context);
                const auto value =
                    dsp_mailbox_read_high(dsp_cpu_mailbox(context));
                if ((value & 0x8000u) != 0u ||
                    context.host_wake == nullptr ||
                    !dsp_is_rmge01_cmbh_poll_pc(context.pc)) {
                    trace_value(value);
                    return value;
                }

                // Snapshot first, then immediately recheck abort, PIINT, and
                // the mailbox. CPU-mail publication increments generation
                // after its release mailbox write. If publication races after
                // this recheck but before wait(), atomic::wait observes the
                // changed generation and returns without losing the wake.
                const auto generation = context.host_wake->generation.load(
                    std::memory_order_acquire);
                dsp_throw_if_host_abort_requested(context);
                dsp_accept_pending_external_interrupt(context);
                const auto rechecked_value =
                    dsp_mailbox_read_high(dsp_cpu_mailbox(context));
                if ((rechecked_value & 0x8000u) != 0u) {
                    trace_value(rechecked_value);
                    return rechecked_value;
                }

                trace_value(rechecked_value);
                dsp_native_telemetry_publish_pending_worker_counts(context);
                context.host_wake->idle_wait_count.fetch_add(
                    1u, std::memory_order_release);
                context.host_wake->generation.wait(
                    generation, std::memory_order_acquire);
            }
        }
    case kDspIfxCmbl:
        dsp_throw_if_host_abort_requested(context);
        dsp_accept_pending_external_interrupt(context);
        {
            bool consumed_mail = false;
            std::uint16_t value{};
            const bool busy =
                (dsp_mailbox_read_high(dsp_cpu_mailbox(context)) & 0x8000u) !=
                0u;
            if (busy &&
                context.hardware.consume_cpu_mailbox_low != nullptr) {
                if (context.host_cpu_mail_generation == nullptr) {
                    dsp_hard_trap_at_current(
                        context,
                        "DSP CMBL ordering hook lacks mail generation");
                }
                const auto generation =
                    context.host_cpu_mail_generation->load(
                        std::memory_order_acquire);
                if (generation == 0u ||
                    !context.hardware.consume_cpu_mailbox_low(
                        context.hardware_user,
                        generation,
                        &dsp_cpu_mailbox(context),
                        &value,
                        &consumed_mail) ||
                    !consumed_mail) {
                    dsp_hard_trap_at_current(
                        context,
                        "DSP busy CMBL consume ordering was rejected");
                }
            } else {
                value = dsp_mailbox_read_low(
                    dsp_cpu_mailbox(context), &consumed_mail);
            }
            if (consumed_mail && context.host_telemetry != nullptr) {
                dsp_native_telemetry_publish_pending_worker_counts(context);
                dsp_native_telemetry_add(
                    context.host_telemetry->cpu_mail_consumed);
            }
            static std::atomic<std::uint64_t> s_cmbl_reads{0};
            static std::atomic<std::uint64_t> s_cmbl_key_reads{0};
            if ((dsp_trace_ifx_key_mail_pc(context) &&
                 dsp_trace_ifx_sample(s_cmbl_key_reads, 512u, 4096u)) ||
                dsp_trace_ifx_sample(s_cmbl_reads, 256u, 4096u)) {
                dsp_trace_ifx_mail(context, "read CMBL", value);
            }
            return value;
        }
    case kDspIfxAcdsamp:
        return dsp_accelerator_read_sample(context);
    case kDspIfxAcdraw:
        return dsp_accelerator_read_raw(context);
    default:
        if (reg < kDspIfxCoefA1Base) {
            dsp_hard_trap_at_current(context, "DSP IFX read from unsupported register");
        }
        return context.ifx[reg];
    }
}

inline void dsp_ifx_write(DspContext& context, std::uint16_t address, std::uint16_t value) {
    const auto reg = static_cast<std::uint16_t>(address & 0x00ffu);
    switch (reg) {
    case kDspIfxDirq:
        dsp_trace_ifx_mail(context, "write DIRQ", value);
        if ((value & 1u) != 0u) {
            if (context.hardware.request_interrupt == nullptr) {
                dsp_hard_trap_at_current(context, "DSP DIRQ write without interrupt callback");
            }
            if (context.hardware.before_dsp_interrupt != nullptr &&
                !context.hardware.before_dsp_interrupt(
                    context.hardware_user)) {
                dsp_hard_trap_at_current(
                    context,
                    "DSP DIRQ pre-publication ordering was rejected");
            }
            dsp_native_telemetry_publish_pending_worker_counts(context);
            if (context.host_telemetry != nullptr) {
                dsp_native_telemetry_add(context.host_telemetry->dirq_writes);
            }
            dsp_publish_diagnostic_snapshot(
                context, DspDiagnosticBoundary::DspInterruptRequest);
            context.hardware.request_interrupt(context.hardware_user);
        } else if (value != 0u) {
            dsp_hard_trap_at_current(context, "DSP DIRQ write has unsupported value");
        }
        return;
    case kDspIfxDmbh:
        dsp_mailbox_write_high(dsp_dsp_mailbox(context), value);
        dsp_trace_ifx_mail(context, "write DMBH", value);
        return;
    case kDspIfxDmbl:
        if (context.hardware.before_dsp_mailbox_low != nullptr &&
            !context.hardware.before_dsp_mailbox_low(
                context.hardware_user)) {
            dsp_hard_trap_at_current(
                context,
                "DSP DMBL pre-publication ordering was rejected");
        }
        dsp_mailbox_write_low_and_publish_diagnostic(context, value);
        dsp_native_telemetry_publish_pending_worker_counts(context);
        if (context.host_telemetry != nullptr) {
            dsp_native_telemetry_add(
                context.host_telemetry->dsp_mail_published);
        }
        dsp_trace_ifx_mail(context, "write DMBL", value);
        dsp_trace_task_mail_context(context);
        return;
    case kDspIfxCmbh:
        dsp_mailbox_write_high(dsp_cpu_mailbox(context), value);
        dsp_trace_ifx_mail(context, "write CMBH", value);
        return;
    case kDspIfxCmbl:
        (void)dsp_mailbox_write_low(dsp_cpu_mailbox(context), value);
        dsp_trace_ifx_mail(context, "write CMBL", value);
        return;
    case kDspIfxDscr:
    case kDspIfxDspa:
    case kDspIfxDsmah:
    case kDspIfxDsmal:
    case kDspIfxAmdm:
        context.ifx[reg] = value;
        return;
    case kDspIfxDsbl:
        context.ifx[kDspIfxDsbl] = value;
        context.ifx[kDspIfxDscr] =
            static_cast<std::uint16_t>(context.ifx[kDspIfxDscr] | 0x0004u);
        dsp_run_dma(context);
        context.ifx[kDspIfxDscr] =
            static_cast<std::uint16_t>(context.ifx[kDspIfxDscr] & ~0x0004u);
        context.ifx[kDspIfxDsbl] = 0;
        return;
    case kDspIfxAcsah:
        dsp_accelerator_write_pair_high(
            context, kDspIfxAcsah, kDspIfxAcsal, value, 0x3fffffffu);
        return;
    case kDspIfxAcsal:
        dsp_accelerator_write_pair_low(
            context, kDspIfxAcsah, kDspIfxAcsal, value, 0x3fffffffu);
        return;
    case kDspIfxAceah:
        dsp_accelerator_write_pair_high(
            context, kDspIfxAceah, kDspIfxAceal, value, 0x3fffffffu);
        return;
    case kDspIfxAceal:
        dsp_accelerator_write_pair_low(
            context, kDspIfxAceah, kDspIfxAceal, value, 0x3fffffffu);
        return;
    case kDspIfxAccah:
        dsp_accelerator_write_pair_high(
            context, kDspIfxAccah, kDspIfxAccal, value, 0xbfffffffu);
        return;
    case kDspIfxAccal:
        dsp_accelerator_write_pair_low(
            context, kDspIfxAccah, kDspIfxAccal, value, 0xbfffffffu);
        return;
    case kDspIfxFormat:
    case kDspIfxGain:
    case kDspIfxYn1:
    case kDspIfxAcin:
        context.ifx[reg] = value;
        return;
    case kDspIfxYn2:
        context.ifx[reg] = value;
        context.accelerator_reads_stopped = false;
        return;
    case kDspIfxPredScale:
        context.ifx[reg] = static_cast<std::uint16_t>(value & 0x007fu);
        return;
    case kDspIfxAcdraw:
        dsp_accelerator_write_raw(context, value);
        return;
    default:
        if (reg < kDspIfxCoefA1Base) {
            dsp_hard_trap_at_current(context, "DSP IFX write to unsupported register");
        }
        context.ifx[reg] = value;
        return;
    }
}

// Address ranges not decoded into DRAM/COEF/IFX below are wired to nothing on
// real silicon: this fixed-function DSP has no MMU or access-fault mechanism,
// so hardware simply reads back an undefined value (0) and ignores writes.
// Dolphin's DSP LLE core -- which runs real DSP microcode bit-exactly -- does
// the same (ReadDMEM/WriteDMEM log an error and return 0 / drop the write;
// they never fault). Confirmed reachable during ordinary gameplay: a real,
// legitimate JAudio2 call site feeds a non-positional/BGM channel's distance
// term with a deliberate "not applicable" sentinel, which this ucode's
// predictor-index computation turns into an address like 0x7FFF -- outside
// DRAM/COEF/IFX, but not a corrupted/unsupported program on real hardware.
inline void dsp_trace_unmapped_data_access(
    const DspContext& context, std::uint16_t address, const char* verb) {
    if (!dsp_trace_mem_summary_enabled()) {
        return;
    }
    static std::atomic<std::uint64_t> s_unmapped_count{0};
    const std::uint64_t count = dsp_trace_ifx_next_count(s_unmapped_count);
    if (count <= 512u || (count % 4096u) == 0u) {
        std::fprintf(
            stderr,
            "[dsp-unmapped-data] #%llu pc=0x%04X %s address=0x%04X\n",
            static_cast<unsigned long long>(count),
            static_cast<unsigned>(context.pc),
            verb,
            static_cast<unsigned>(address));
    }
}

inline std::uint16_t dsp_data_read(DspContext& context, std::uint16_t address) {
    switch (address & 0xf000u) {
    case 0x0000u:
        return dsp_dram_read(context, address);
    case 0x1000u:
        if (!context.coef_loaded) {
            dsp_hard_trap_at_current(context, "DSP coefficient ROM read before initialization");
        }
        return context.coef[static_cast<std::size_t>(address & 0x07ffu)];
    case 0xf000u:
        return dsp_ifx_read(context, address);
    default:
        dsp_trace_unmapped_data_access(context, address, "read");
        return 0u;
    }
}

inline void dsp_data_write(DspContext& context, std::uint16_t address, std::uint16_t value) {
    switch (address & 0xf000u) {
    case 0x0000u:
        dsp_dram_write(context, address, value);
        return;
    case 0xf000u:
        dsp_ifx_write(context, address, value);
        return;
    default:
        dsp_trace_unmapped_data_access(context, address, "write");
        static_cast<void>(value);
        return;
    }
}

}  // namespace galaxy
