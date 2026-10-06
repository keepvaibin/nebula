#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace galaxy::timing {

inline constexpr std::uint64_t kTimelineTicksPerSecond = 60'750'000ull;
inline constexpr std::uint64_t kViPeriodTicks =
    kTimelineTicksPerSecond / 60ull;
// Lowered from 1.5 VI periods: a stall just over one VI period (the smallest
// possible backlog=1 miss) with near-zero CPU time used is still genuine
// evidence of an OS scheduling gap, not simulation load, and is exactly the
// case the fatal cadence check has no other tolerance for. This only widens
// which intervals are considered as a recovery *candidate* -- the separate
// candidate_proof_matches identity re-verification and the atomic
// rebase_and_restore_after_host_suspension transaction in
// recover_from_verified_host_suspension are unchanged and still required
// before anything is actually restored.
inline constexpr std::uint64_t kHostSuspensionCandidateMinimumTicks =
    kViPeriodTicks;
inline constexpr std::uint64_t
    kHostSuspensionCandidateMaximumCpuTime100ns = 10'000u;

// Classifies only the wall-time/CPU evidence for a possible host scheduling
// suspension. Recovery still requires unchanged deadline identity, a fresh
// observation, and an atomic timeline/deadline restore. The ordinary path
// also requires an exact VI backlog with inactive delivery scopes; the sole
// active-scope exception is separately proven by the durable VI/AI policy.
[[nodiscard]] constexpr bool is_host_suspension_candidate_interval(
    std::uint64_t elapsed_ticks,
    std::uint64_t elapsed_cpu_time_100ns) noexcept {
    return elapsed_ticks >= kHostSuspensionCandidateMinimumTicks &&
        elapsed_cpu_time_100ns <=
            kHostSuspensionCandidateMaximumCpuTime100ns;
}

// Proves that the interval classified above began before one exact device
// deadline and ended at least a full device period after it. Subtractions are
// performed only after ordering is established, so every uint64 boundary
// fails closed without overflow.
[[nodiscard]] constexpr bool host_suspension_crossed_device_deadline_by_period(
    std::uint64_t last_guest_ticks,
    std::uint64_t observed_ticks,
    std::uint64_t deadline_ticks,
    std::uint64_t period_ticks) noexcept {
    return last_guest_ticks != 0u && observed_ticks != 0u &&
        deadline_ticks != 0u && period_ticks != 0u &&
        last_guest_ticks < deadline_ticks &&
        observed_ticks >= deadline_ticks &&
        observed_ticks - deadline_ticks >= period_ticks;
}

using CounterReadFn = std::uint64_t (*)(void* user) noexcept;

struct CounterSource {
    CounterReadFn read{};
    void* user{};
    std::uint64_t frequency{};
};

// Returns a process-wide QueryPerformanceCounter source. The source owns no
// resources and remains valid for the process lifetime.
CounterSource query_performance_counter_source();

// Converts a counter delta into the Wii's 60.75 MHz time-base domain without
// floating point. Overflow saturates and is reported through `saturated`.
std::uint64_t scale_counter_delta(
    std::uint64_t counter_delta,
    std::uint64_t counter_frequency,
    bool* saturated = nullptr) noexcept;

// One monotonic time domain shared by VI, AI, input, decrementer, and DSP
// visibility. Checkpoint counts are deliberately absent from this type.
class RuntimeTimeline {
public:
    explicit RuntimeTimeline(
        CounterSource source,
        std::uint64_t initial_ticks = 0);

    // Samples the host-derived timeline. Broker publication and recovery
    // preparation may inspect an uncommitted host suspension interval. Before
    // exposing a returned value to a guest or device owner, the simulation
    // thread must commit_guest_observation after any safe recovery decision.
    // Optional diagnostic output is the exact raw counter read made by this
    // call, before monotonic clamping; it performs no additional clock read.
    [[nodiscard]] std::uint64_t now_ticks(
        std::uint64_t* sampled_counter = nullptr) noexcept;
    // Commits a guest/device-visible observation. Equal observations are
    // allowed, but a regression is a fatal caller error. Recovery cannot
    // discard an interval containing a committed observation.
    void commit_guest_observation(std::uint64_t ticks);
    [[nodiscard]] std::uint64_t committed_guest_ticks() const noexcept {
        return committed_guest_ticks_.load(std::memory_order_acquire);
    }
    // Preflight before touching deadline owners or pending publications. The
    // simulation thread serializes commits and recovery; DeadlineBroker also
    // holds its worker mutex while revalidating this guard before mutation.
    [[nodiscard]] bool can_rebase_after_host_suspension(
        std::uint64_t last_guest_ticks,
        std::uint64_t observed_host_ticks) const noexcept;
    // Rebases the host-counter mapping after a verified host suspension.  The
    // caller must serialize this operation against DeadlineBroker publication
    // and supply the last guest-visible tick plus the immediately observed
    // host-derived tick.  This intentionally preserves the guest's monotonic
    // clock while excluding an interval in which the simulation thread made
    // no progress; it is not a cadence re-phase or an event coalescer.
    [[nodiscard]] bool rebase_after_host_suspension(
        std::uint64_t last_guest_ticks,
        std::uint64_t observed_host_ticks) noexcept;
    [[nodiscard]] std::uint64_t counter_frequency() const noexcept {
        return source_.frequency;
    }
    [[nodiscard]] bool saturated() const noexcept {
        return saturated_.load(std::memory_order_acquire);
    }

private:
    [[nodiscard]] std::optional<std::uint64_t>
    host_suspension_discarded_ticks(
        std::uint64_t last_guest_ticks,
        std::uint64_t observed_host_ticks) const noexcept;
    CounterSource source_{};
    std::uint64_t epoch_counter_{};
    std::uint64_t epoch_ticks_{};
    std::atomic_uint64_t discarded_host_ticks_{};
    std::atomic_uint64_t last_counter_{};
    std::atomic_uint64_t last_ticks_{};
    std::atomic_uint64_t committed_guest_ticks_{};
    std::atomic_bool saturated_{false};
};

enum class EventKind : std::uint8_t {
    VideoInterface = 0,
    AudioDma = 1,
    InputReport = 2,
    Decrementer = 3,
    Dsp = 4,
    RuntimeFailure = 5,
    // Native storage completion is a host wake, not a guest interrupt or a
    // periodic device deadline. Guest IPC IRQ eligibility still obeys MSR[EE].
    IosCompletion = 6,
    Count = 7,
};

inline constexpr std::size_t kEventKindCount =
    static_cast<std::size_t>(EventKind::Count);

[[nodiscard]] constexpr std::uint32_t event_bit(EventKind kind) noexcept {
    return 1u << static_cast<unsigned>(kind);
}

struct DeadlineEvent {
    EventKind kind{EventKind::VideoInterface};
    std::uint64_t sequence{};
    std::uint64_t deadline_ticks{};
    bool replaceable{};
};

struct PeriodicDeadlineRestore {
    EventKind kind{EventKind::VideoInterface};
    std::uint64_t first_deadline_ticks{};
    std::uint64_t period_ticks{};
    std::uint64_t first_sequence{};
};

struct ReplaceableDeadlineRestore {
    EventKind kind{EventKind::AudioDma};
    std::uint64_t deadline_ticks{};
    std::uint64_t generation{};
};

// Lower/upper bounds around the exact latch-publication call.  The worker
// samples the shared Wii timeline immediately before and after publication so
// a cadence failure can distinguish a broker that woke late from a consumer
// that failed to reach a generated safepoint.  A pre-publication lateness at
// least one period is conclusive broker-side evidence; a post-publication
// lateness below one period is conclusive consumer-side evidence.
struct DeadlinePublicationStats {
    std::uint64_t publications{};
    std::uint64_t last_sequence{};
    std::uint64_t last_deadline_ticks{};
    std::uint64_t last_prepublication_ticks{};
    std::uint64_t last_postpublication_ticks{};
    std::uint64_t last_prepublication_lateness_ticks{};
    std::uint64_t last_postpublication_lateness_ticks{};
    std::uint64_t max_prepublication_lateness_ticks{};
    std::uint64_t max_postpublication_lateness_ticks{};
};

class PeriodicDeadline {
public:
    PeriodicDeadline(
        std::uint64_t first_deadline_ticks,
        std::uint64_t period_ticks,
        std::uint64_t first_sequence = 1);

    [[nodiscard]] std::uint64_t next_deadline_ticks() const noexcept {
        return next_deadline_ticks_;
    }
    [[nodiscard]] std::uint64_t next_sequence() const noexcept {
        return next_sequence_;
    }
    [[nodiscard]] std::uint64_t period_ticks() const noexcept {
        return period_ticks_;
    }

    // Returns every elapsed edge. It never advances past an edge without
    // returning it; arithmetic overflow throws instead of silently dropping
    // deadlines.
    std::vector<DeadlineEvent> collect_due(
        EventKind kind,
        std::uint64_t now_ticks,
        std::size_t maximum_events = 1'000'000);

private:
    std::uint64_t next_deadline_ticks_{};
    std::uint64_t period_ticks_{};
    std::uint64_t next_sequence_{};
};

// Deterministic schedule policy. A future timer thread may call collect_due()
// and publish the results, but it must never call translated guest code.
class DeadlineSchedule {
public:
    void set_periodic(
        EventKind kind,
        std::uint64_t first_deadline_ticks,
        std::uint64_t period_ticks,
        std::uint64_t first_sequence = 1);
    void cancel_periodic(EventKind kind) noexcept;

    void schedule_once(
        EventKind kind,
        std::uint64_t deadline_ticks,
        std::optional<std::uint64_t> sequence = std::nullopt);

    // Keeps at most one replaceable one-shot for `kind`.  The caller owns the
    // monotonically increasing generation so a safepoint can prove that a
    // publication still describes the currently armed device deadline.
    void replace_once(
        EventKind kind,
        std::uint64_t deadline_ticks,
        std::uint64_t generation);
    // Reinstates the one currently owned replaceable generation after a
    // verified host-suspension timeline rebase. Unlike replace_once(), this
    // intentionally requires equality with the already-owned generation: the
    // device did not advance while the simulation was suspended, so assigning
    // it a new generation would manufacture a device transition.
    void restore_replaceable_after_host_suspension(
        EventKind kind,
        std::uint64_t deadline_ticks,
        std::uint64_t generation);
    void cancel_once(EventKind kind, std::uint64_t generation);

    [[nodiscard]] std::optional<std::uint64_t> next_deadline_ticks() const;
    std::vector<DeadlineEvent> collect_due(
        std::uint64_t now_ticks,
        std::size_t maximum_events = 1'000'000);

private:
    static std::size_t index(EventKind kind);

    std::array<std::optional<PeriodicDeadline>, kEventKindCount> periodic_{};
    std::array<std::uint64_t, kEventKindCount> next_one_shot_sequence_{};
    std::array<std::uint64_t, kEventKindCount>
        latest_replaceable_generation_{};
    std::vector<DeadlineEvent> one_shots_{};
};

// Synchronized publication boundary between deadline/device threads and the
// translated-code safepoint. Per-kind spin locks make each payload plus wake
// bit one publication protocol; counts retain every event even when the
// bitmask coalesces wakeups.
class PendingEventLatch {
public:
    struct PeriodicBatch {
        std::uint64_t count{};
        std::uint64_t first_sequence{};
        std::uint64_t last_sequence{};
        std::uint64_t first_deadline_ticks{};
        std::uint64_t last_deadline_ticks{};

        [[nodiscard]] bool has_identity() const noexcept {
            return count != 0u && first_sequence != 0u &&
                last_sequence != 0u;
        }
    };

    enum class ExactTakeStatus : std::uint8_t {
        Empty,
        Taken,
        Backlog,
    };

    struct ExactTakeResult {
        ExactTakeStatus status{ExactTakeStatus::Empty};
        std::uint64_t observed_count{};
    };

    enum class PeriodicPrefixTakeStatus : std::uint8_t {
        Empty,
        Taken,
        Incomplete,
        Invalid,
        ArithmeticOverflow,
    };

    struct PeriodicPrefixTakeResult {
        PeriodicPrefixTakeStatus status{PeriodicPrefixTakeStatus::Empty};
        PeriodicBatch batch{};
        std::uint64_t observed_count{};
    };

    enum class ReplaceableTakeStatus : std::uint8_t {
        Empty,
        Taken,
        Stale,
        Future,
    };

    struct ReplaceableTakeResult {
        ReplaceableTakeStatus status{ReplaceableTakeStatus::Empty};
        std::uint64_t observed_generation{};
    };

    // Failure-only telemetry for distinguishing a delayed deadline consumer
    // from contention in this publication boundary. The counters saturate so
    // collecting diagnostics can never wrap into a misleading low value.
    struct LockContentionStats {
        std::uint64_t contended_acquisitions{};
        std::uint64_t total_spins{};
        std::uint64_t max_spins{};
    };

    PendingEventLatch() = default;

    bool publish(EventKind kind, std::uint64_t count = 1) noexcept;
    // Publishes one exact periodic edge. Identity and count are updated under
    // the same per-kind lock, so a consumer can never drain a singleton count
    // while accidentally observing the following edge's sequence/deadline.
    // Mixing anonymous and identity-bearing publications for one pending kind
    // is rejected instead of manufacturing an identity for anonymous work.
    bool publish_periodic(const DeadlineEvent& event) noexcept;
    // Publishes one generation-bearing edge. A second unconsumed generation
    // is an invariant violation rather than a count that can lose identity.
    bool publish_replaceable(
        EventKind kind,
        std::uint64_t generation) noexcept;
    [[nodiscard]] std::uint64_t pending(EventKind kind) const noexcept;
    [[nodiscard]] std::uint64_t pending_replaceable_generation(
        EventKind kind) const noexcept;
    [[nodiscard]] LockContentionStats lock_contention_stats(
        EventKind kind) const noexcept;
    [[nodiscard]] std::uint32_t mask() const noexcept {
        return mask_.load(std::memory_order_acquire);
    }
    [[nodiscard]] const std::atomic_uint32_t* mask_address() const noexcept {
        return &mask_;
    }

    bool take_one(EventKind kind) noexcept;
    std::uint64_t take_all(EventKind kind) noexcept;
    // Atomically drains both the pending count and the immutable identity
    // range that produced it. An anonymous publication returns count with
    // zero identity fields, allowing an exact consumer to fail closed.
    [[nodiscard]] PeriodicBatch take_all_periodic(
        EventKind kind) noexcept;

    // Atomically consumes only the exact, contiguous due prefix observed by a
    // level-triggered consumer. A producer may append a later edge between the
    // consumer's clock sample and this transaction; that suffix remains
    // latched with its original identity and wake bit instead of being drained
    // and misclassified against the older clock sample.
    [[nodiscard]] PeriodicPrefixTakeResult take_periodic_prefix_if_matches(
        EventKind kind,
        std::uint64_t expected_first_sequence,
        std::uint64_t expected_first_deadline_ticks,
        std::uint64_t period_ticks,
        std::uint64_t due_count) noexcept;

    // Linearizes only when the pending count is exactly one. Concurrent
    // publishers cannot turn a previously observed singleton into a hidden
    // backlog: the compare/exchange reports Backlog and consumes nothing.
    [[nodiscard]] ExactTakeResult take_one_if_only(EventKind kind) noexcept;

    // Consumes only the expected generation. An older publication is removed
    // and reported as stale; a newer publication is preserved and reported as
    // future so callers cannot accidentally service the wrong device edge.
    [[nodiscard]] ReplaceableTakeResult take_replaceable(
        EventKind kind,
        std::uint64_t expected_generation) noexcept;
    void clear_replaceable(EventKind kind) noexcept;

private:
    static std::size_t index(EventKind kind) noexcept;
    void lock_slot(std::size_t slot) noexcept;
    void unlock_slot(std::size_t slot) noexcept;
    void repair_mask_while_locked(
        std::size_t slot,
        EventKind kind) noexcept;
    void clear_periodic_identity_while_locked(std::size_t slot) noexcept;

    std::array<std::atomic_uint64_t, kEventKindCount> counts_{};
    std::array<std::atomic_uint64_t, kEventKindCount>
        replaceable_generations_{};
    // Accessed only while holding slot_locks_[slot]. Counts remain atomic for
    // the lock-free pending() fast path; the compound identity deliberately
    // is not observable outside the linearized drain operation.
    std::array<PeriodicBatch, kEventKindCount> periodic_batches_{};
    // Zero denotes a fresh singleton whose cadence cannot yet be inferred.
    // Once a second identity is appended, every later deadline must retain
    // this exact step. Prefix consumption preserves it for a pending suffix.
    std::array<std::uint64_t, kEventKindCount> periodic_period_ticks_{};
    // The payload atomics and their shared wake bit form one publication
    // protocol. Serialize each kind so a consumer cannot drain the payload
    // between a producer's payload store and mask update, leaving a stale bit.
    std::array<std::atomic_flag, kEventKindCount> slot_locks_{};
    // These fields are not part of the publication protocol. They are updated
    // only after a contended acquisition succeeds and are read only for
    // failure diagnostics, with relaxed ordering sufficient for both uses.
    std::array<std::atomic_uint64_t, kEventKindCount>
        contended_acquisitions_{};
    std::array<std::atomic_uint64_t, kEventKindCount> total_spins_{};
    std::array<std::atomic_uint64_t, kEventKindCount> max_spins_{};
    std::atomic_uint32_t mask_{0};
};

enum class PeriodicConsumeStatus : std::uint8_t {
    NotDue,
    AwaitingPublication,
    Ready,
    EarlyPublication,
    Backlog,
    ArithmeticOverflow,
};

enum class HostSuspensionRecoveryProof : std::uint8_t {
    ViBacklog,
    AiOverrunWithOwnedActiveViSuccessor,
    BacklogWithOwnedActiveViSuccessor,
};

// Select a proof to validate; this never authorizes a timeline rebase itself.
// An active backlog must not select the inactive proof merely because its
// periodic observation says Backlog. The caller supplies the existing exact
// successor predicate and revalidates all device/transaction identities.
[[nodiscard]] constexpr std::optional<HostSuspensionRecoveryProof>
select_host_suspension_recovery_proof(
    PeriodicConsumeStatus status,
    bool boundary_active,
    bool vi_delivery_active,
    bool ready_successor_ai_crossed,
    bool active_backlog_successor_owned,
    bool active_backlog_recovery_enabled) noexcept {
    if (status == PeriodicConsumeStatus::Backlog) {
        if (!boundary_active && !vi_delivery_active) {
            return HostSuspensionRecoveryProof::ViBacklog;
        }
        if (boundary_active && vi_delivery_active &&
            active_backlog_recovery_enabled &&
            active_backlog_successor_owned) {
            return HostSuspensionRecoveryProof::
                BacklogWithOwnedActiveViSuccessor;
        }
    } else if (status == PeriodicConsumeStatus::Ready &&
               boundary_active && vi_delivery_active &&
               ready_successor_ai_crossed) {
        return HostSuspensionRecoveryProof::
            AiOverrunWithOwnedActiveViSuccessor;
    }
    return std::nullopt;
}

struct PeriodicDeadlineObservation {
    PeriodicConsumeStatus status{PeriodicConsumeStatus::NotDue};
    std::uint64_t expected_sequence{};
    std::uint64_t expected_deadline_ticks{};
    std::uint64_t pending_edges{};
    std::uint64_t scheduled_due_edges{};
    std::uint64_t scheduled_edges_elapsed{};
    std::uint64_t lateness_ticks{};
    std::uint64_t missed_or_coalesced_edges{};
};

struct PeriodicDeadlineAuditStats {
    std::uint64_t scheduled_edges{};
    std::uint64_t delivered_edges{};
    // delivered_edges counts exact schedule identities acknowledged by the
    // consumer. A level-triggered device may acknowledge several elapsed
    // identities through one guest-visible interrupt; these counters keep
    // that hardware coalescing explicit instead of disguising it as perfect
    // one-interrupt-per-edge delivery.
    std::uint64_t delivered_interrupts{};
    std::uint64_t coalesced_level_edges{};
    std::uint64_t level_coalescing_events{};
    std::uint64_t last_scheduled_sequence{};
    std::uint64_t last_delivered_sequence{};
    std::uint64_t missed_or_coalesced_edges{};
    std::uint64_t max_pending_backlog{};
    std::uint64_t max_lateness_ticks{};
    std::uint64_t early_publication_edges{};
    std::uint64_t arithmetic_failures{};
    bool locked_rate_valid{true};
};

// Single-consumer policy for a periodic hardware edge. Publishers may run on
// arbitrary threads through PendingEventLatch. Deadlines are always rebuilt
// from the immutable origin rather than advanced from an observation, so no
// late poll can silently redefine cadence. Once strict consumption detects an
// invalid cadence the policy remains failed and all later consumption throws.
class ExactPeriodicDeadlineConsumer {
public:
    ExactPeriodicDeadlineConsumer(
        std::uint64_t first_deadline_ticks,
        std::uint64_t period_ticks,
        std::uint64_t first_sequence = 1);

    [[nodiscard]] PeriodicDeadlineObservation inspect(
        const PendingEventLatch& latch,
        EventKind kind,
        std::uint64_t now_ticks) const noexcept;

    // Returns no event when the edge is not due or its publication has not yet
    // linearized. Throws without consuming when cadence is early, backlogged,
    // coalesced, or unrepresentable. A successful event is the one exact edge
    // whose pending count changed atomically from one to zero.
    std::optional<DeadlineEvent> consume_one_or_throw(
        PendingEventLatch& latch,
        EventKind kind,
        std::uint64_t now_ticks);

    // Level-triggered hardware retains an asserted level, not an unbounded
    // queue of interrupt entries. This variant preserves the strict schedule
    // and validates a backlog's complete sequence/deadline identity, but when
    // several already-due levels accumulated it acknowledges their latest
    // identity through one guest-visible interrupt. Singleton delivery keeps
    // the strict method's transaction. It never coalesces an early,
    // anonymous, incomplete, noncontiguous, or arithmetically invalid batch.
    // An incomplete due batch returns no event without consuming anything;
    // even a prior Ready inspection can defer if another period elapses.
    // The strict method above remains available for edge-triggered consumers
    // and acceptance tests that require one delivery per edge.
    std::optional<DeadlineEvent> consume_one_or_coalesce_level_or_throw(
        PendingEventLatch& latch,
        EventKind kind,
        std::uint64_t now_ticks);

    [[nodiscard]] const PeriodicDeadlineAuditStats& stats() const noexcept {
        return stats_;
    }

private:
    [[nodiscard]] PeriodicDeadlineObservation inspect_count(
        std::uint64_t pending_edges,
        std::uint64_t now_ticks) const noexcept;
    void account_observation(
        const PeriodicDeadlineObservation& observation) noexcept;
    [[noreturn]] void fail(
        const PeriodicDeadlineObservation& observation,
        const char* message,
        bool arithmetic_failure);

    std::uint64_t first_deadline_ticks_{};
    std::uint64_t period_ticks_{};
    std::uint64_t first_sequence_{};
    std::uint64_t delivered_edges_{};
    PeriodicDeadlineAuditStats stats_{};
};

// Owns only host-side deadline publication. The worker latches due events and
// wakes translated safepoints through PendingEventLatch; it never calls guest
// functions or reads/writes PpcContext.
class DeadlineBroker {
public:
    DeadlineBroker(RuntimeTimeline& timeline, PendingEventLatch& latch);
    ~DeadlineBroker();

    DeadlineBroker(const DeadlineBroker&) = delete;
    DeadlineBroker& operator=(const DeadlineBroker&) = delete;

    void set_periodic(
        EventKind kind,
        std::uint64_t first_deadline_ticks,
        std::uint64_t period_ticks,
        std::uint64_t first_sequence = 1);
    void cancel_periodic(EventKind kind) noexcept;
    void schedule_once(
        EventKind kind,
        std::uint64_t deadline_ticks,
        std::optional<std::uint64_t> sequence = std::nullopt);
    void replace_once(
        EventKind kind,
        std::uint64_t deadline_ticks,
        std::uint64_t generation);
    // Restores an already-owned replaceable device deadline after a verified
    // host suspension. The generation must equal the broker's current owner;
    // this is not a general replacement API.
    void restore_replaceable_after_host_suspension(
        EventKind kind,
        std::uint64_t deadline_ticks,
        std::uint64_t generation);
    void cancel_once(EventKind kind, std::uint64_t generation);

    void start();
    void stop() noexcept;
    [[nodiscard]] bool failed() const noexcept {
        return failed_.load(std::memory_order_acquire);
    }
    [[nodiscard]] DeadlinePublicationStats publication_stats(
        EventKind kind) const;
    // Serializes a verified host-suspension clock rebase against the worker's
    // timeline reads.  Callers must re-arm any device schedules whose already
    // published edges were invalidated by the rebase before guest execution
    // resumes.
    [[nodiscard]] bool rebase_timeline_after_host_suspension(
        std::uint64_t last_guest_ticks,
        std::uint64_t observed_host_ticks);
    // Atomically restores all deadline owners and rebases the timeline after
    // a verified host suspension. The caller prepares the immutable recovery
    // description without entering guest code; this operation then excludes
    // that native-only preparation time from the guest clock and prevents the
    // worker from publishing a stale edge between the rebase and re-arm.
    [[nodiscard]] bool rebase_and_restore_after_host_suspension(
        std::uint64_t last_guest_ticks,
        std::uint64_t observed_host_ticks,
        const std::vector<PeriodicDeadlineRestore>& periodic_restores,
        const std::vector<ReplaceableDeadlineRestore>& replaceable_restores);
    void throw_if_failed();

private:
    void worker_loop() noexcept;
    void notify_worker() noexcept;

    RuntimeTimeline& timeline_;
    PendingEventLatch& latch_;
    DeadlineSchedule schedule_{};
    mutable std::mutex mutex_{};
    // Auto-reset Win32 event stored opaquely to keep this public runtime
    // contract free of platform headers. The worker combines it with a
    // high-resolution waitable timer, so schedule changes never wait behind a
    // coarse condition-variable timeout.
    void* wake_event_{};
    std::thread worker_{};
    bool started_{};
    bool stopping_{};
    std::atomic_bool failed_{false};
    std::exception_ptr failure_{};
    std::array<DeadlinePublicationStats, kEventKindCount>
        publication_stats_{};
};

}  // namespace galaxy::timing
