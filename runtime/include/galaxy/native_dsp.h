#pragma once

#include "galaxy/dsp_context.h"
#include "galaxy/dsp_guest_bridge.h"
#include "galaxy/native_api.h"

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>

namespace galaxy {

inline constexpr std::uint32_t kNativeDspMmioCpuMailboxHigh = 0x5000;
inline constexpr std::uint32_t kNativeDspMmioCpuMailboxLow = 0x5002;
inline constexpr std::uint32_t kNativeDspMmioDspMailboxHigh = 0x5004;
inline constexpr std::uint32_t kNativeDspMmioDspMailboxLow = 0x5006;
inline constexpr std::uint32_t kNativeDspMmioControl = 0x500a;
inline constexpr std::uint16_t kNativeDspControlReset = 0x0001;
inline constexpr std::uint16_t kNativeDspControlHalt = 0x0004;

// One-slot rendezvous between the free-running native DSP execution thread and
// the CPU/simulation thread that exclusively owns live guest MRAM. The DSP
// publishes one fixed-capacity request, emits one safepoint wake, and blocks
// until the CPU commits the complete span or a bounded failure occurs. There
// is deliberately no request queue and no worker-side GuestMemory alias. The
// owner must call shutdown() and join its submitting worker before destroying
// this boundary; GuestAddressSpace enforces that lifetime order explicitly.
class DspMramTransactionBoundary {
public:
    static constexpr std::uint32_t kMaximumSpanBytes = 0x4000u;
    static constexpr std::chrono::milliseconds kDefaultTimeout{2000};

    enum class Direction : std::uint8_t {
        ReadFromMram,
        WriteToMram,
    };

    enum class ServiceResult : std::uint8_t {
        None,
        Completed,
        Rejected,
        WrongThread,
    };

    struct Snapshot {
        std::uint64_t read_requests{};
        std::uint64_t write_requests{};
        std::uint64_t wake_publications{};
        std::uint64_t wake_failures{};
        std::uint64_t service_calls{};
        std::uint64_t completed_services{};
        std::uint64_t rejected_requests{};
        std::uint64_t failed_services{};
        std::uint64_t timeout_failures{};
        std::uint64_t shutdown_cancellations{};
        std::uint32_t in_flight{};
        std::uint32_t maximum_in_flight{};
        bool failed{};
        bool shutdown{};
    };

    using WakeCallback = bool (*)(void* user) noexcept;
    using ReadService = bool (*)(
        void* user,
        std::uint32_t address,
        std::uint8_t* destination,
        std::uint32_t size) noexcept;
    using WriteService = bool (*)(
        void* user,
        std::uint32_t address,
        const std::uint8_t* source,
        std::uint32_t size) noexcept;

    explicit DspMramTransactionBoundary(
        std::chrono::milliseconds timeout = kDefaultTimeout);
    ~DspMramTransactionBoundary();

    DspMramTransactionBoundary(const DspMramTransactionBoundary&) = delete;
    DspMramTransactionBoundary& operator=(
        const DspMramTransactionBoundary&) = delete;

    // Configuration is legal only with no request in flight. reset() binds
    // service ownership to its caller and clears a prior shutdown/failure;
    // cumulative telemetry intentionally survives service restarts.
    [[nodiscard]] bool configure_wake(
        WakeCallback callback,
        void* user) noexcept;
    [[nodiscard]] bool reset(
        std::chrono::milliseconds timeout = kDefaultTimeout) noexcept;

    [[nodiscard]] bool submit_read(
        std::uint32_t address,
        std::uint8_t* destination,
        std::uint32_t size) noexcept;
    [[nodiscard]] bool submit_write(
        std::uint32_t address,
        const std::uint8_t* source,
        std::uint32_t size) noexcept;

    // Must run on the thread that called reset(). A successful write service
    // callback owns both the whole-span commit and its one dirty notification.
    // Callbacks run while the one-slot latch is held, so they must be bounded,
    // nonblocking, and must not re-enter this boundary. The product callbacks
    // are exactly span validation + memcpy (+ one nonblocking dirty
    // publication whose runtime callback only marks GX atomics/readiness
    // telemetry and does not poll the DSP service).
    // The timeout bounds wake/scheduling starvation; it cannot preempt an
    // arbitrary test callback that violates this fixed-service contract.
    [[nodiscard]] ServiceResult service_one(
        void* user,
        ReadService read,
        WriteService write) noexcept;

    // Wakes a blocked submitter and prevents future requests until reset().
    void shutdown() noexcept;
    [[nodiscard]] bool pending() const noexcept;
    [[nodiscard]] Snapshot snapshot() const noexcept;

private:
    enum class State : std::uint8_t {
        Idle,
        Pending,
        Completed,
        Failed,
        Shutdown,
    };

    [[nodiscard]] bool submit(
        Direction direction,
        std::uint32_t address,
        std::uint8_t* read_destination,
        const std::uint8_t* write_source,
        std::uint32_t size) noexcept;
    static void increment(std::uint64_t& counter) noexcept;

    mutable std::mutex mutex_{};
    std::condition_variable completion_cv_{};
    std::array<std::uint8_t, kMaximumSpanBytes> bytes_{};
    WakeCallback wake_callback_{};
    void* wake_user_{};
    std::thread::id service_thread_{};
    std::chrono::milliseconds timeout_{kDefaultTimeout};
    Direction direction_{Direction::ReadFromMram};
    State state_{State::Idle};
    std::uint32_t address_{};
    std::uint32_t size_{};
    bool submitter_active_{};
    Snapshot telemetry_{};
};

// Ownership boundary for the DSP sample accelerator's 14 MiB ARAM window.
// The CPU never receives a pointer to mirror_, and the DSP worker never
// receives a GuestMemory pointer. Instead, the CPU copies one generation-bound
// seed/page packet into this boundary before publishing the corresponding
// hardware mailbox. The DSP worker must consume that mail only through
// worker_apply_before_mail_consume(), which installs the packet exactly once
// and then invokes the callback that performs the real CMBL consume.
//
// The Wii CPU->DSP mailbox itself has one hardware slot, so this boundary also
// has exactly one preallocated inbound slot. Keeping that slot occupied until
// the consume callback succeeds prevents the next generation from overwriting
// data while the prior hardware mail is still busy. Queue-full, generation,
// thread-ownership, and callback failures are sticky hard failures until a
// quiescent reset. No method allocates after construction.
//
// DSP writes remain worker-owned. They are collected as exact per-page dirty
// ranges and exposed only by worker_flush_outbound() as coalesced whole-span
// callbacks. A product callback must copy each span into a CPU-thread
// transaction boundary; it must never dereference GuestMemory on the worker.
// Integration must flush successfully before publishing a DSP completion mail
// or DIRQ. Those mailbox/IRQ hooks are deliberately outside this reusable
// class; bypassing either ordering API is a fatal integration error.
class DspAramMirrorBoundary {
public:
    static constexpr std::uint32_t kMirrorSizeBytes = 14u * 1024u * 1024u;
    static constexpr std::uint32_t kDirtyPageBytes = 128u;
    static constexpr std::uint32_t kDirtyPageCount =
        kMirrorSizeBytes / kDirtyPageBytes;
    static constexpr std::uint32_t kInboundQueueCapacity = 1u;
    // Enforced by implementation and tests: valid worker mirror reads/writes
    // never acquire mutex_. CPU packet staging and snapshotting therefore
    // cannot contend with the accelerator sample hot path.
    static constexpr bool kWorkerHotPathUsesBoundaryMutex = false;

    struct DirtyPageUpdate {
        std::uint32_t page_index{};
        std::span<const std::uint8_t> bytes{};
    };

    enum class Failure : std::uint8_t {
        None,
        InvalidArgument,
        WrongCpuThread,
        WrongWorkerThread,
        WorkerNotBound,
        ResetWhileWorkerBound,
        QueueFull,
        GenerationMismatch,
        SeedRequired,
        DuplicateSeed,
        PendingInbound,
        UnflushedOutbound,
        MailConsumeRejected,
        OutboundSpanRejected,
        ReentrantOperation,
    };

    struct Snapshot {
        std::uint64_t resets{};
        std::uint64_t cancellations{};
        std::uint64_t shutdowns{};
        std::uint64_t seed_packets_staged{};
        std::uint64_t dirty_packets_staged{};
        std::uint64_t dirty_pages_staged{};
        std::uint64_t seed_packets_applied{};
        std::uint64_t dirty_packets_applied{};
        std::uint64_t dirty_pages_applied{};
        std::uint64_t mail_consume_callbacks{};
        std::uint64_t mail_consume_rejections{};
        std::uint64_t worker_read_spans{};
        std::uint64_t worker_read_bytes{};
        std::uint64_t worker_write_spans{};
        std::uint64_t worker_write_bytes{};
        std::uint64_t worker_word_writes{};
        std::uint64_t outbound_flushes{};
        std::uint64_t outbound_span_attempts{};
        std::uint64_t outbound_span_callbacks{};
        std::uint64_t outbound_bytes{};
        std::uint64_t outbound_span_rejections{};
        std::uint64_t maximum_outbound_spans_per_flush{};
        std::uint64_t maximum_outbound_bytes_per_flush{};
        std::uint64_t hard_failures{};
        std::uint64_t generation_failures{};
        std::uint64_t queue_full_failures{};
        std::uint64_t wrong_thread_failures{};
        std::uint64_t invalid_argument_failures{};
        std::uint64_t worker_hot_telemetry_publications{};
        std::uint64_t cancelled_inbound_packets{};
        std::uint64_t cancelled_outbound_pages{};
        std::uint32_t inbound_depth{};
        std::uint32_t maximum_inbound_depth{};
        std::uint32_t outbound_dirty_pages{};
        std::uint64_t last_cpu_staged_generation{};
        std::uint64_t last_cpu_consumed_generation{};
        std::uint64_t last_outbound_generation{};
        Failure failure{Failure::None};
        bool worker_bound{};
        bool seeded{};
        bool failed{};
        bool cancelled{};
        bool shutdown{};
    };

    // Runs while this boundary's mutex is held so the hardware CMBL consume
    // and inbound-slot retirement are one indivisible publication boundary.
    // The callback must therefore be non-reentrant: it may perform the real
    // lock-free mailbox consume (and valid lock-free worker reads), but must
    // not call boundary staging, mutation, snapshot, flush, failure, or
    // shutdown operations.
    using MailConsumeCallback =
        bool (*)(void* user, std::uint64_t generation) noexcept;
    using OutboundSpanCallback = bool (*)(
        void* user,
        std::uint64_t generation,
        std::uint32_t address,
        const std::uint8_t* source,
        std::uint32_t size) noexcept;

    DspAramMirrorBoundary();
    ~DspAramMirrorBoundary();

    DspAramMirrorBoundary(const DspAramMirrorBoundary&) = delete;
    DspAramMirrorBoundary& operator=(const DspAramMirrorBoundary&) = delete;

    // reset() binds CPU ownership to its caller. A bound worker must first be
    // cancelled/shut down, joined or otherwise quiesced, and detached. Reset
    // zeros the mirror and discards all prior generations and dirty state.
    [[nodiscard]] bool reset() noexcept;
    void cancel() noexcept;
    void shutdown() noexcept;

    // Must be called by the actual DSP execution thread before any worker API.
    // worker_detach() is legal after cancel/failure/shutdown so teardown can
    // always release the identity. An active detach with queued input or
    // unflushed output is a sticky hard failure.
    [[nodiscard]] bool worker_bind() noexcept;
    [[nodiscard]] bool worker_detach() noexcept;

    // CPU-owner operations. The full seed is required once per reset. Dirty
    // pages must be strictly increasing and unique; an empty page list is a
    // valid no-change packet for a mail generation. The packet must be staged
    // before the matching mailbox low-half release publication.
    [[nodiscard]] bool cpu_stage_seed(
        std::uint64_t generation,
        std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] bool cpu_stage_dirty_pages(
        std::uint64_t generation,
        std::span<const DirtyPageUpdate> pages) noexcept;

    // Worker operation that proves ordering rather than merely documenting it:
    // the matching packet is installed once, then consume() performs the real
    // CMBL read. The inbound slot is released only after consume() succeeds.
    // consume() executes under the boundary mutex and must obey the
    // non-reentrant MailConsumeCallback contract above.
    [[nodiscard]] bool worker_apply_before_mail_consume(
        std::uint64_t generation,
        MailConsumeCallback consume,
        void* user) noexcept;

    [[nodiscard]] bool worker_read_span(
        std::uint32_t address,
        std::uint8_t* destination,
        std::uint32_t size) noexcept;
    [[nodiscard]] bool worker_read_u16_be(
        std::uint32_t address,
        std::uint16_t* value) noexcept;
    [[nodiscard]] bool worker_write_span(
        std::uint32_t address,
        const std::uint8_t* source,
        std::uint32_t size) noexcept;
    // Validates both bytes first, commits them under one worker-owned critical
    // section, and records one logical word write. Accelerator ACDRAW must use
    // this API rather than two byte calls.
    [[nodiscard]] bool worker_write_u16_be(
        std::uint32_t address,
        std::uint16_t value) noexcept;

    // generation identifies the DSP completion-mail/IRQ publication that the
    // caller will perform only after this returns true. Every callback is one
    // complete, contiguous transaction. A rejection is terminal, because a
    // prefix may already have reached the CPU and cannot safely be replayed.
    // source is borrowed only for the synchronous callback duration: the
    // callback must copy it into its own preallocated CPU transaction and may
    // neither retain the pointer nor hand it to another thread.
    [[nodiscard]] bool worker_flush_outbound(
        std::uint64_t generation,
        OutboundSpanCallback commit,
        void* user) noexcept;

    [[nodiscard]] Snapshot snapshot() const noexcept;

private:
    enum class State : std::uint8_t {
        Dormant,
        Active,
        Cancelled,
        Failed,
        Shutdown,
    };

    enum class InboundKind : std::uint8_t {
        Seed,
        DirtyPages,
    };

    struct Storage;

    [[nodiscard]] bool cpu_thread_is_current_locked() noexcept;
    [[nodiscard]] bool worker_thread_is_current_locked() noexcept;
    [[nodiscard]] bool stage_packet_locked(
        InboundKind kind,
        std::uint64_t generation) noexcept;
    void mark_outbound_dirty_worker(
        std::uint32_t address,
        std::uint32_t size) noexcept;
    void clear_inbound_locked() noexcept;
    void clear_outbound_full_locked() noexcept;
    void clear_outbound_sparse_locked() noexcept;
    void fail_locked(Failure failure) noexcept;
    [[nodiscard]] bool fail_worker_hot(Failure failure) noexcept;
    void publish_worker_telemetry_locked() noexcept;
    [[nodiscard]] static bool span_is_valid(
        std::uint32_t address,
        const void* pointer,
        std::uint32_t size) noexcept;
    static void increment(std::uint64_t& counter) noexcept;
    static void add(std::uint64_t& counter, std::uint64_t value) noexcept;

    mutable std::mutex mutex_{};
    std::unique_ptr<Storage> storage_{};
    std::thread::id cpu_thread_{};
    std::thread::id worker_thread_{};
    std::atomic<State> state_{State::Dormant};
    Failure failure_{Failure::None};
    bool cpu_thread_bound_{};
    bool worker_thread_bound_{};
    bool seed_staged_{};
    bool worker_seeded_{};
    bool mail_callback_active_{};
    bool outbound_callback_active_{};
    std::uint32_t inbound_depth_{};
    std::uint32_t outbound_dirty_page_count_{};
    std::atomic<std::uint32_t> published_outbound_dirty_page_count_{};
    std::uint64_t last_cpu_staged_generation_{};
    std::uint64_t last_cpu_consumed_generation_{};
    std::uint64_t last_outbound_generation_{};
    Snapshot telemetry_{};
};

class NativeDspCoprocessor {
public:
    using EntryPoint = void (*)(DspContext& context);
    using InterruptCallback = void (*)(void* user);
    using AcceleratorExceptionCallback =
        void (*)(void* user, DspAcceleratorException exception);

    NativeDspCoprocessor();

    void reset();
    void connect_guest_memory(
        GuestMemoryV1* memory,
        const NativeServicesV1* services,
        std::uint32_t guest_pc);
    void set_host_callbacks(
        void* user,
        InterruptCallback interrupt,
        AcceleratorExceptionCallback accelerator_exception);

    // Install a fully host-owned hardware environment, replacing the default
    // guest-memory bridge. Used by the native runtime to route the DSP DMA bus
    // (MRAM) and the sample-accelerator bus (ARAM) through its own address
    // translation and to deliver interrupts to the audio path. host_abort is
    // preserved.
    void install_host_services(void* user, const DspHardwareServices& services);

    // Direct context access is a stopped/single-threaded setup-and-test API.
    // It hard-fails while a NativeDspWorker owns the context; live product
    // diagnostics use latest_diagnostic_snapshot() and control decisions use
    // NativeDspWorker state instead.
    DspContext& context() noexcept;
    const DspContext& context() const noexcept;

    // Enables bounded worker-published snapshots at DMBL, DIRQ, and worker
    // return boundaries. Must be configured before start(); the disabled path
    // installs no publisher pointer and therefore performs no copy or lock.
    void set_diagnostic_capture_enabled(bool enabled);
    [[nodiscard]] std::optional<DspDiagnosticSnapshot>
    latest_diagnostic_snapshot() const;

    // Enables the bounded worker-owned DSP audio causal recorder. Configure
    // before start(); snapshot only after the worker has stopped.
    void set_audio_causal_capture_enabled(bool enabled);
    [[nodiscard]] bool audio_causal_capture_enabled() const noexcept {
        return audio_causal_capture_enabled_;
    }
    [[nodiscard]] DspAudioCausalSnapshot audio_causal_snapshot() const;

    // Enables a standalone, coprocessor-owned exact RMGE01 selected-channel
    // diagnostic. Configure before start(); snapshot only after worker stop.
    // Product code instead attaches its process-lifetime recorder below so a
    // DSPCR reset cannot discard prior evidence.
    void set_channel_selection_dma_probe_enabled(bool enabled);
    void attach_channel_selection_dma_probe(
        DspChannelSelectionDmaProbeRecorder* recorder);
    void attach_audio_bus_controls(
        const audio::NativeAudioBusControls* controls);
    [[nodiscard]] bool channel_selection_dma_probe_enabled() const noexcept {
        return channel_selection_dma_probe_enabled_;
    }
    [[nodiscard]] DspChannelSelectionDmaProbeSnapshot
    channel_selection_dma_probe_snapshot() const;

    void load_iram_words(std::uint16_t destination, std::span<const std::uint16_t> words);
    void load_irom_words(std::span<const std::uint16_t> words);
    void load_dram_words(std::uint16_t destination, std::span<const std::uint16_t> words);
    void load_coefficient_words(std::span<const std::uint16_t> words);
    void load_iram_bytes(std::uint32_t destination_byte, std::span<const std::byte> bytes);
    void load_dram_bytes(std::uint32_t destination_byte, std::span<const std::byte> bytes);

    void cpu_write_to_dsp_mailbox_high(std::uint16_t value);
    void cpu_write_to_dsp_mailbox_low(std::uint16_t value);
    // Product mail publication reserves its generation before staging the
    // matching ARAM packet, then releases the busy low half only after staging
    // succeeds. The ordinary low-write API is the single-threaded convenience
    // wrapper around this exact two-step contract.
    [[nodiscard]] std::uint64_t reserve_cpu_mail_generation();
    void publish_cpu_mailbox_low(
        std::uint16_t value,
        std::uint64_t generation);
    std::uint16_t cpu_read_from_dsp_mailbox_high() const;
    std::uint16_t cpu_read_from_dsp_mailbox_low();
    std::uint16_t cpu_peek_from_dsp_mailbox_low() const;

    // True while the DSP has posted a mail the CPU has not yet consumed
    // (non-destructive). False once cpu_read_from_dsp_mailbox_low() drains it.
    [[nodiscard]] bool dsp_mail_present() const {
        return (cpu_read_from_dsp_mailbox_high() & 0x8000u) != 0u;
    }
    // True while a CPU-to-DSP mail is still waiting for the DSP to pick it up.
    [[nodiscard]] bool cpu_mail_consumed() const {
        return (cpu_peek_to_dsp_mailbox_high() & 0x8000u) == 0u;
    }
    // Monotonic identity of the most recent CPU->DSP mailbox publication.
    // The worker uses this to make conditional HALT resumes idempotent: two
    // wake attempts for one still-busy hardware mail are one event, while a
    // later mail (after the DSP consumed the first) is a distinct event.
    [[nodiscard]] std::uint64_t cpu_mail_generation() const noexcept {
        return cpu_mail_generation_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t host_idle_wait_count() const noexcept {
        return host_wake_state_.idle_wait_count.load(std::memory_order_acquire);
    }
    [[nodiscard]] DspNativeTelemetrySnapshot telemetry_snapshot() const noexcept {
        auto snapshot = dsp_native_telemetry_snapshot(telemetry_state_);
        snapshot.host_idle_waits = host_idle_wait_count();
        return snapshot;
    }
    std::uint16_t cpu_peek_to_dsp_mailbox_high() const;
    std::uint16_t cpu_peek_to_dsp_mailbox_low() const;

    std::uint16_t cpu_read_mmio16(std::uint32_t offset);
    void cpu_write_mmio16(std::uint32_t offset, std::uint16_t value);

    // Run a compiled entry that returns (single-shot; e.g. unit-test probes).
    void run_native_entry(EntryPoint entry);

    // Run the compiled ucode until it returns. A return can be cooperative
    // abort from a mailbox poll, an accepted external interrupt, or a lowered
    // DSP HALT that left ctx.pc on the HALT instruction. Returns true only when
    // the return was caused by an accepted external interrupt.
    [[nodiscard]] bool run_free_running(EntryPoint entry);
    void request_abort();
    void clear_abort();
    [[nodiscard]] bool abort_requested() const;
    void record_hard_trap(std::uint16_t pc);
    void clear_hard_trap();
    [[nodiscard]] bool hard_trap_active() const;
    [[nodiscard]] std::uint16_t hard_trap_pc() const;
    void request_external_interrupt(std::uint16_t vector);
    [[nodiscard]] bool enter_pending_external_interrupt();

    // Wake a free-running compiled entry parked in its host-assisted CMBH
    // idle wait. CPU-mail publication, abort, and external interrupt already
    // call this; NativeDspWorker::signal_work() uses it for an explicit poke.
    void notify_mailbox_poll();

private:
    friend class NativeDspWorker;

    void attach_bridge();
    void require_no_active_worker() const noexcept;
    void claim_context_for_worker();
    void release_context_from_worker();
    [[nodiscard]] DspContext& worker_context() noexcept { return context_; }
    void publish_worker_diagnostic(DspDiagnosticBoundary boundary);
    [[nodiscard]] DspDiagnosticSnapshot capture_quiescent_worker_snapshot(
        std::uint64_t completed_runs) const;

    DspContext context_{};
    DspGuestMemoryBridge bridge_{};
    std::uint16_t control_{};
    std::atomic<bool> abort_flag_{};
    std::atomic<bool> hard_trap_flag_{};
    std::atomic<std::uint16_t> hard_trap_pc_{};
    std::atomic<std::uint16_t> external_interrupt_vector_{
        kDspNoPendingExternalInterrupt};
    std::atomic<std::uint64_t> cpu_mail_generation_{};
    DspHostWakeState host_wake_state_{};
    DspNativeTelemetryState telemetry_state_{};
    // Allocated only when the explicit causal diagnostic is enabled so the
    // normal coprocessor object (and every test fixture) does not carry the
    // fixed recorder payload.
    std::unique_ptr<DspAudioCausalRecorder> audio_causal_recorder_{};
    std::unique_ptr<DspChannelSelectionDmaProbeRecorder>
        owned_channel_selection_dma_probe_{};
    DspChannelSelectionDmaProbeRecorder* channel_selection_dma_probe_{};
    const audio::NativeAudioBusControls* audio_bus_controls_{};
    mutable DspDiagnosticState diagnostic_state_{};
    bool diagnostic_capture_enabled_{};
    bool audio_causal_capture_enabled_{};
    bool channel_selection_dma_probe_enabled_{};
    std::atomic<bool> worker_owns_context_{};
    // Written only by the thread executing generated DSP code, then flushed
    // into telemetry_state_ at observable hardware boundaries.
    std::uint64_t pending_instruction_entries_{};
    DspPendingIdleBackEdgeCounters pending_idle_backedges_{};
    DspRmge01IdleSequenceTracker idle_sequence_tracker_{};
};

class NativeDspWorker {
public:
    // free_running=false: one entry() execution per signal_work() (probe model).
    // free_running=true: the compiled ucode runs until it reaches a lowered
    // HALT or cooperative abort. signal_resume() and signal_external_interrupt()
    // are the only ways to leave HALT; signal_work() merely wakes mailbox polls.
    NativeDspWorker(
        NativeDspCoprocessor& coprocessor,
        NativeDspCoprocessor::EntryPoint entry,
        bool free_running = false,
        DspAramMirrorBoundary* aram_boundary = nullptr);
    ~NativeDspWorker();

    NativeDspWorker(const NativeDspWorker&) = delete;
    NativeDspWorker& operator=(const NativeDspWorker&) = delete;

    void start();
    void stop();
    void signal_work();
    void signal_resume(std::uint16_t resume_pc);
    // Resume a published HALT, or retain the request across the narrow
    // generated-entry-return -> HALT-state-publication interval. A request
    // made while Executing is accepted only for a still-busy CPU mailbox and
    // is discarded if that same mailbox generation is consumed before HALT.
    // Duplicate calls for one mailbox generation are idempotent.
    [[nodiscard]] bool signal_resume_if_waiting(std::uint16_t resume_pc);
    void signal_external_interrupt(std::uint16_t vector);
    // Scheduler-owned state, safe for behavior/control decisions. Diagnostic
    // snapshots are evidence only and must never drive wake/interrupt policy.
    [[nodiscard]] bool is_halted() const;
    // Test/inspection API: returns a coherent bounded context copy only while
    // the worker is mutex-published Halted. It never reads executing state.
    [[nodiscard]] std::optional<DspDiagnosticSnapshot> halted_snapshot() const;
    [[nodiscard]] std::uint64_t completed_runs() const;
    [[nodiscard]] bool wait_for_completed_runs(
        std::uint64_t target,
        std::chrono::milliseconds timeout);

private:
    enum class WorkerState : std::uint8_t {
        Stopped,
        Starting,
        Executing,
        Halted,
        Stopping,
        Failed,
    };

    enum class ResumeRequirement : std::uint8_t {
        None,
        CpuMailStillPending,
    };

    struct ResumeLatch {
        std::uint16_t pc{};
        std::uint64_t request_generation{};
        ResumeRequirement requirement{ResumeRequirement::None};
        std::uint64_t cpu_mail_generation{};
    };

    void thread_main();
    void latch_resume_locked(
        std::uint16_t resume_pc,
        ResumeRequirement requirement,
        std::uint64_t cpu_mail_generation);
    void resolve_resume_locked();

    NativeDspCoprocessor* coprocessor_{};
    NativeDspCoprocessor::EntryPoint entry_{};
    bool free_running_{};
    DspAramMirrorBoundary* aram_boundary_{};
    mutable std::mutex mutex_{};
    std::condition_variable work_cv_{};
    std::condition_variable completed_cv_{};
    std::thread thread_{};
    std::uint64_t pending_runs_{};
    std::uint64_t completed_runs_{};
    std::optional<ResumeLatch> pending_resume_{};
    std::optional<std::uint16_t> pending_external_interrupt_pc_{};
    std::uint64_t next_resume_generation_{};
    std::uint64_t resolved_resume_generation_{};
    std::uint64_t resolved_cpu_mail_generation_{};
    WorkerState state_{WorkerState::Stopped};
    bool running_{};
    bool stop_requested_{};
};

}  // namespace galaxy
