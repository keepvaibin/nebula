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
// Lowered from 1.5 VI periods. A stall just over one VI period (the smallest backlog=1 miss) with little CPU
// time used is still an indication of an OS scheduling gap, not simulation load. The fatal cadence check has no
// other tolerance for it. This only widens which intervals count as recovery candidates. candidate_proof_matches
// and the atomic rebase_and_restore_after_host_suspension transaction in recover_from_verified_host_suspension
// still check and restore before anything changes.
inline constexpr std::uint64_t kHostSuspensionCandidateMinimumTicks =
    kViPeriodTicks;
inline constexpr std::uint64_t
    kHostSuspensionCandidateMaximumCpuTime100ns = 10'000u;

// Classifies only the wall-time and CPU data for a possible host scheduling suspension. Recovery still requires
// unchanged deadline identity, a fresh observation and an atomic timeline and deadline restore. The ordinary path
// also requires an exact VI backlog with inactive delivery scopes. The only active-scope exception is checked
// separately by the VI/AI policy.
[[nodiscard]] constexpr bool is_host_suspension_candidate_interval(
    std::uint64_t elapsed_ticks,
    std::uint64_t elapsed_cpu_time_100ns) noexcept {
    return elapsed_ticks >= kHostSuspensionCandidateMinimumTicks &&
        elapsed_cpu_time_100ns <=
            kHostSuspensionCandidateMaximumCpuTime100ns;
}

// Proves the classified interval began before one exact device deadline and ended at least one full
// device period after it. Subtraction happens only after ordering is established, so every uint64
// boundary fails closed without overflow.
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

// Returns a raw time-stamp-counter source (rdtsc divided by 8, to stay in the supported frequency range)
// when the processor reports an invariant TSC. It is calibrated against QueryPerformanceCounter over
// `calibration_ms`. A timeline read then costs one rdtsc instead of a QueryPerformanceCounter call,
// because the simulation thread reads the time base over 100,000 times per second (OSGetTick/OSGetTime,
// checkpoints, device deadlines). Empty when the TSC is not invariant or calibration fails.
std::optional<CounterSource> invariant_tsc_counter_source(unsigned calibration_ms);

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

    // Samples the host-derived timeline. Broker publication and recovery preparation may inspect an
    // uncommitted host suspension interval. Before a returned value reaches a guest or device owner, the
    // simulation thread must call commit_guest_observation, after any safe recovery decision. Optional
    // diagnostic output is the exact raw counter read by this call, before monotonic clamping. It makes no
    // extra clock read.
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
    // Rebases the host-counter mapping after a host suspension that passed its checks. The caller serializes this
    // against DeadlineBroker publication and supplies the last guest-visible tick plus the host-derived tick just
    // observed. The guest clock stays monotonic and excludes the interval in which the simulation thread made no
    // progress. This is not a cadence re-phase or an event coalescer.
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
    // ticks per counter unit as a 0.64 fixed-point fraction, used instead of
    // the two divisions of scale_counter_delta when the counter is faster
    // than the time base (every TSC source); 0 selects the exact path.
    std::uint64_t scale_fixed64_{};
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
    // Flag-based interrupt return: the simulation thread sets this bit when
    // an RFI resumes elsewhere and clears it at the flat dispatcher. It is a
    // mask bit only (no deadline, no count); it makes every translated
    // checkpoint escalate so the native stack unwinds by plain returns.
    Transfer = 7,
    Count = 8,
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

// Bounds around the exact latch-publication call. The worker samples the shared Wii timeline just before and
// after publication, so a cadence failure can tell a late broker from a consumer that missed a generated
// safepoint. Lateness before publication of at least one period identifies a broker-side delay. Lateness after
// publication below one period identifies a consumer-side delay.
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
    // Reinstates the one currently owned replaceable generation after a host-suspension timeline rebase. Unlike
    // replace_once(), this intentionally requires equality with the already-owned generation: the device did not
    // advance while the simulation was suspended, so assigning it a new generation would manufacture a device
    // transition.
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
    // Wakes a simulation thread blocked in WaitOnAddress on mask_address().
    // `source` is the EventKind that published (timeline attribution only);
    // 100 marks a host wake that is not a latch publication (PE, IOS).
    void wake_idle_waiters(std::uint32_t source = 100u) noexcept;
    void set_transfer_pending() noexcept;
    void clear_transfer_pending() noexcept;
    // Optional auto-reset event that wake_idle_waiters also signals. The simulation thread's idle wait
    // blocks on it together with a high-resolution waitable timer, because WaitOnAddress timeouts are
    // quantized to the system clock interrupt (about 16 to 33 ms).
    void set_idle_wake_event(void* event) noexcept {
        idle_wake_event_.store(event, std::memory_order_release);
    }
    // The simulation thread announces that it is about to block on the wake
    // event (and re-tests the mask afterwards); publishers signal the event
    // only while this is set, so a publication from a thread that is not
    // waited on costs no kernel transition. Sequentially consistent on both
    // sides so a publication can never slip between the test and the wait.
    void set_idle_waiting(bool waiting) noexcept {
        idle_waiting_.store(waiting, std::memory_order_seq_cst);
    }
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

    // Linearizes only when the pending count is exactly one. Concurrent publishers cannot turn an observed singleton
    // into a hidden backlog: the compare/exchange reports Backlog and consumes nothing.
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
    std::atomic<void*> idle_wake_event_{nullptr};
    std::atomic<bool> idle_waiting_{false};
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
    // delivered_edges counts exact schedule identities acknowledged by the consumer. A level-triggered
    // device may acknowledge several elapsed identities through one guest-visible interrupt. These
    // counters keep that hardware coalescing explicit, rather than presenting it as one interrupt per edge.
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

    // Level-triggered hardware retains an asserted level, not an unbounded queue of interrupt entries. This
    // variant keeps the strict schedule and checks a backlog's complete sequence and deadline identity. When
    // several due levels have accumulated, it acknowledges the latest identity with one guest-visible
    // interrupt. A singleton uses the strict method's transaction. It never coalesces an early, anonymous,
    // incomplete, noncontiguous or arithmetically invalid batch. An incomplete due batch returns no event
    // and consumes nothing. Even a prior Ready inspection can be deferred if another period elapses. The
    // strict method remains for edge-triggered consumers and acceptance tests that need one delivery per edge.
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
    // Restores an already-owned replaceable device deadline after a host suspension. The generation must equal the
    // broker's current owner; this is not a general replacement API.
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
    // Serializes a host-suspension clock rebase against the worker's timeline reads. Callers re-arm any device
    // schedule whose published edges the rebase invalidated before guest execution resumes.
    [[nodiscard]] bool rebase_timeline_after_host_suspension(
        std::uint64_t last_guest_ticks,
        std::uint64_t observed_host_ticks);
    // Atomically restores all deadline owners and rebases the timeline after a host suspension. The caller prepares
    // the immutable recovery description without entering guest code. This operation then excludes that native-only
    // preparation time from the guest clock, and stops the worker from publishing a stale edge between the rebase
    // and re-arm.
    [[nodiscard]] bool rebase_and_restore_after_host_suspension(
        std::uint64_t last_guest_ticks,
        std::uint64_t observed_host_ticks,
        const std::vector<PeriodicDeadlineRestore>& periodic_restores,
        const std::vector<ReplaceableDeadlineRestore>& replaceable_restores);
    void throw_if_failed();
    // Census: schedule changes and the subset that had to wake the worker.
    [[nodiscard]] std::uint64_t schedule_changes() const noexcept {
        return schedule_changes_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t worker_wakes() const noexcept {
        return worker_wakes_.load(std::memory_order_relaxed);
    }

private:
    void worker_loop() noexcept;
    void notify_worker() noexcept;
    std::atomic<std::uint64_t> schedule_changes_{0};
    std::atomic<std::uint64_t> worker_wakes_{0};

    RuntimeTimeline& timeline_;
    PendingEventLatch& latch_;
    DeadlineSchedule schedule_{};
    mutable std::mutex mutex_{};
    // The deadline the worker is currently sleeping toward (all ones while it
    // waits for a signal only). A new or replaced deadline wakes it only when
    // it is earlier; a later one is picked up when the worker next recomputes.
    std::uint64_t worker_wait_until_ticks_ = 0u;
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
