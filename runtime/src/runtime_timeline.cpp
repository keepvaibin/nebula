#include "galaxy/runtime_timeline.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace galaxy::timing {
namespace {

constexpr std::uint64_t kMaximumSupportedCounterFrequency = 1'000'000'000ull;

std::uint64_t read_query_performance_counter(void*) noexcept {
    LARGE_INTEGER value{};
    if (QueryPerformanceCounter(&value) == FALSE || value.QuadPart < 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(value.QuadPart);
}

std::uint64_t saturating_add(
    std::uint64_t lhs,
    std::uint64_t rhs,
    bool& saturated) noexcept {
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        saturated = true;
        return std::numeric_limits<std::uint64_t>::max();
    }
    return lhs + rhs;
}

void saturating_atomic_add(
    std::atomic_uint64_t& value,
    std::uint64_t increment) noexcept {
    std::uint64_t observed = value.load(std::memory_order_relaxed);
    while (observed != std::numeric_limits<std::uint64_t>::max()) {
        bool saturated = false;
        const std::uint64_t desired =
            saturating_add(observed, increment, saturated);
        if (value.compare_exchange_weak(
                observed,
                desired,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            return;
        }
    }
}

void update_atomic_max(
    std::atomic_uint64_t& value,
    std::uint64_t candidate) noexcept {
    std::uint64_t observed = value.load(std::memory_order_relaxed);
    while (candidate > observed &&
           !value.compare_exchange_weak(
               observed,
               candidate,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

std::uint64_t checked_add(
    std::uint64_t lhs,
    std::uint64_t rhs,
    const char* message) {
    if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
        throw std::overflow_error(message);
    }
    return lhs + rhs;
}

std::uint64_t checked_multiply(
    std::uint64_t lhs,
    std::uint64_t rhs,
    const char* message) {
    if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        throw std::overflow_error(message);
    }
    return lhs * rhs;
}

bool event_less(const DeadlineEvent& lhs, const DeadlineEvent& rhs) noexcept {
    if (lhs.deadline_ticks != rhs.deadline_ticks) {
        return lhs.deadline_ticks < rhs.deadline_ticks;
    }
    if (lhs.kind != rhs.kind) {
        return static_cast<unsigned>(lhs.kind) <
            static_cast<unsigned>(rhs.kind);
    }
    return lhs.sequence < rhs.sequence;
}

std::chrono::nanoseconds timeline_wait_duration(
    std::uint64_t delta_ticks) noexcept {
    constexpr std::uint64_t kNanosecondsPerSecond = 1'000'000'000ull;
    constexpr std::uint64_t kMaximumWaitSeconds = 60ull * 60ull;
    const std::uint64_t whole_seconds =
        std::min(
            delta_ticks / kTimelineTicksPerSecond,
            kMaximumWaitSeconds);
    const std::uint64_t remainder =
        delta_ticks % kTimelineTicksPerSecond;
    const std::uint64_t fractional_nanoseconds =
        (remainder * kNanosecondsPerSecond +
         kTimelineTicksPerSecond - 1u) /
        kTimelineTicksPerSecond;
    return std::chrono::seconds(whole_seconds) +
        std::chrono::nanoseconds(fractional_nanoseconds);
}

void configure_deadline_worker_scheduling() {
    // The simulation thread runs at ABOVE_NORMAL and, when available, in the
    // Games MMCSS class. A default-priority broker can otherwise be starved by
    // the very thread whose hardware deadlines it must publish, especially on
    // older CPUs with few available cores.
    if (SetThreadPriority(
            GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) == FALSE) {
        const DWORD error = GetLastError();
        throw std::system_error(
            static_cast<int>(error),
            std::system_category(),
            "failed to set deadline broker worker priority");
    }

    const int priority = GetThreadPriority(GetCurrentThread());
    if (priority == THREAD_PRIORITY_ERROR_RETURN) {
        const DWORD error = GetLastError();
        throw std::system_error(
            static_cast<int>(error),
            std::system_category(),
            "failed to query deadline broker worker priority");
    }
    if (priority < THREAD_PRIORITY_ABOVE_NORMAL) {
        throw std::runtime_error(
            "deadline broker worker priority remained below ABOVE_NORMAL");
    }
}

// The deadline broker is the producer for the runtime's VI, audio, and input
// deadlines. It does no guest work itself, but it is nevertheless a
// time-sensitive part of the same Games workload as the simulation thread.
// Register the worker independently: MMCSS registration is per-thread, not
// per-process. The existing ABOVE_NORMAL priority remains the safe fallback
// when AVRT/MMCSS is unavailable or has been explicitly disabled for the
// runtime.
class ScopedMmcssDeadlineWorker {
public:
    ScopedMmcssDeadlineWorker() {
        if (!runtime_mmcss_enabled()) {
            return;
        }
        avrt_ = LoadLibraryW(L"avrt.dll");
        if (avrt_ == nullptr) {
            return;
        }
        set_characteristics_ =
            reinterpret_cast<SetCharacteristicsFn>(
                GetProcAddress(avrt_, "AvSetMmThreadCharacteristicsW"));
        set_priority_ = reinterpret_cast<SetPriorityFn>(
            GetProcAddress(avrt_, "AvSetMmThreadPriority"));
        revert_ = reinterpret_cast<RevertFn>(
            GetProcAddress(avrt_, "AvRevertMmThreadCharacteristics"));
        if (set_characteristics_ == nullptr || revert_ == nullptr) {
            return;
        }

        DWORD task_index = 0u;
        task_handle_ = set_characteristics_(L"Games", &task_index);
        if (task_handle_ == nullptr) {
            return;
        }
        if (set_priority_ != nullptr) {
            // AVRT_PRIORITY_HIGH, matching the simulation Games worker. Do
            // not use the critical priority reserved for the audio path.
            constexpr int kAvrtPriorityHigh = 1;
            static_cast<void>(set_priority_(
                task_handle_, kAvrtPriorityHigh));
        }
    }

    ~ScopedMmcssDeadlineWorker() {
        if (task_handle_ != nullptr && revert_ != nullptr) {
            static_cast<void>(revert_(task_handle_));
        }
        if (avrt_ != nullptr) {
            FreeLibrary(avrt_);
        }
    }

    ScopedMmcssDeadlineWorker(const ScopedMmcssDeadlineWorker&) = delete;
    ScopedMmcssDeadlineWorker& operator=(
        const ScopedMmcssDeadlineWorker&) = delete;

private:
    using SetCharacteristicsFn = HANDLE (WINAPI*)(LPCWSTR, LPDWORD);
    using SetPriorityFn = BOOL (WINAPI*)(HANDLE, int);
    using RevertFn = BOOL (WINAPI*)(HANDLE);

    static bool runtime_mmcss_enabled() {
        wchar_t value[16]{};
        const DWORD length = GetEnvironmentVariableW(
            L"GALAXY_RUNTIME_MMCSS",
            value,
            static_cast<DWORD>(sizeof(value) / sizeof(value[0])));
        if (length == 0u || length >= (sizeof(value) / sizeof(value[0]))) {
            return true;
        }
        for (DWORD index = 0u; index < length; ++index) {
            if (value[index] >= L'A' && value[index] <= L'Z') {
                value[index] = static_cast<wchar_t>(
                    value[index] - L'A' + L'a');
            }
        }
        return std::wcscmp(value, L"0") != 0 &&
               std::wcscmp(value, L"false") != 0 &&
               std::wcscmp(value, L"no") != 0 &&
               std::wcscmp(value, L"off") != 0;
    }

    HMODULE avrt_{};
    HANDLE task_handle_{};
    SetCharacteristicsFn set_characteristics_{};
    SetPriorityFn set_priority_{};
    RevertFn revert_{};
};

// condition_variable::wait_for is permitted to share Windows' ordinary timer
// coalescing. The deadline broker is the source of VI/input/audio hardware
// edges, so an ordinary 15 ms-or-larger wake delay can turn one late callback
// into multiple unrepresentable periodic edges. Keep schedule changes on an
// auto-reset event and use a high-resolution waitable timer only for the
// immutable next deadline. Neither handle executes guest code.
class ScopedDeadlineWaitTimer {
public:
    ScopedDeadlineWaitTimer() {
        timer_ = CreateWaitableTimerExW(
            nullptr,
            nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (timer_ == nullptr) {
            timer_ = CreateWaitableTimerExW(
                nullptr,
                nullptr,
                0,
                TIMER_MODIFY_STATE | SYNCHRONIZE);
        }
        if (timer_ == nullptr) {
            const DWORD error = GetLastError();
            throw std::system_error(
                static_cast<int>(error),
                std::system_category(),
                "failed to create deadline broker waitable timer");
        }
    }

    ~ScopedDeadlineWaitTimer() {
        if (timer_ != nullptr) {
            CloseHandle(timer_);
        }
    }

    ScopedDeadlineWaitTimer(const ScopedDeadlineWaitTimer&) = delete;
    ScopedDeadlineWaitTimer& operator=(const ScopedDeadlineWaitTimer&) = delete;

    void wait_for(
        HANDLE wake_event,
        std::chrono::nanoseconds duration) const {
        if (wake_event == nullptr) {
            throw std::runtime_error("deadline broker wake event is unavailable");
        }
        using HundredNanoseconds =
            std::chrono::duration<long long, std::ratio<1, 10'000'000>>;
        const auto ticks =
            std::chrono::duration_cast<HundredNanoseconds>(duration).count();
        if (ticks <= 0) {
            return;
        }
        LARGE_INTEGER due_time{};
        due_time.QuadPart = -ticks;
        if (SetWaitableTimer(
                timer_, &due_time, 0, nullptr, nullptr, FALSE) == FALSE) {
            const DWORD error = GetLastError();
            throw std::system_error(
                static_cast<int>(error),
                std::system_category(),
                "failed to arm deadline broker waitable timer");
        }
        const HANDLE handles[] = {timer_, wake_event};
        const DWORD result = WaitForMultipleObjects(
            static_cast<DWORD>(std::size(handles)), handles, FALSE, INFINITE);
        if (result != WAIT_OBJECT_0 && result != WAIT_OBJECT_0 + 1u) {
            const DWORD error =
                result == WAIT_FAILED ? GetLastError() : ERROR_INVALID_STATE;
            throw std::system_error(
                static_cast<int>(error),
                std::system_category(),
                "deadline broker wait returned an invalid result");
        }
    }

    void wait_for_signal(HANDLE wake_event) const {
        if (wake_event == nullptr) {
            throw std::runtime_error("deadline broker wake event is unavailable");
        }
        if (WaitForSingleObject(wake_event, INFINITE) != WAIT_OBJECT_0) {
            const DWORD error = GetLastError();
            throw std::system_error(
                static_cast<int>(error),
                std::system_category(),
                "deadline broker wake wait failed");
        }
    }

private:
    HANDLE timer_{};
};

}  // namespace

CounterSource query_performance_counter_source() {
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency) == FALSE ||
        frequency.QuadPart <= 0) {
        throw std::runtime_error("QueryPerformanceFrequency failed");
    }
    const auto unsigned_frequency =
        static_cast<std::uint64_t>(frequency.QuadPart);
    if (unsigned_frequency > kMaximumSupportedCounterFrequency) {
        throw std::runtime_error(
            "QueryPerformanceCounter frequency exceeds supported range");
    }
    return CounterSource{
        &read_query_performance_counter,
        nullptr,
        unsigned_frequency};
}

std::uint64_t scale_counter_delta(
    std::uint64_t counter_delta,
    std::uint64_t counter_frequency,
    bool* saturated) noexcept {
    bool did_saturate = false;
    if (counter_frequency == 0 ||
        counter_frequency > kMaximumSupportedCounterFrequency) {
        did_saturate = true;
        if (saturated != nullptr) {
            *saturated = did_saturate;
        }
        return std::numeric_limits<std::uint64_t>::max();
    }

    // At 10 MHz the exact tick ratio reduces to 243/40. Bound the
    // multiplication; larger deltas retain the generic saturation path.
    if (counter_frequency == 10'000'000ull &&
        counter_delta <= std::numeric_limits<std::uint64_t>::max() / 243ull) {
        if (saturated != nullptr) {
            *saturated = false;
        }
        return (counter_delta * 243ull) / 40ull;
    }

    const std::uint64_t whole_seconds = counter_delta / counter_frequency;
    const std::uint64_t remainder = counter_delta % counter_frequency;
    std::uint64_t whole_ticks = 0;
    if (whole_seconds >
        std::numeric_limits<std::uint64_t>::max() /
            kTimelineTicksPerSecond) {
        did_saturate = true;
        whole_ticks = std::numeric_limits<std::uint64_t>::max();
    } else {
        whole_ticks = whole_seconds * kTimelineTicksPerSecond;
    }

    // remainder < 1e9 and kTimelineTicksPerSecond < 1e8, so this product is
    // bounded below 1e17 and cannot overflow uint64_t.
    const std::uint64_t fractional_ticks =
        (remainder * kTimelineTicksPerSecond) / counter_frequency;
    const std::uint64_t result =
        saturating_add(whole_ticks, fractional_ticks, did_saturate);
    if (saturated != nullptr) {
        *saturated = did_saturate;
    }
    return result;
}

RuntimeTimeline::RuntimeTimeline(
    CounterSource source,
    std::uint64_t initial_ticks)
    : source_(source), epoch_ticks_(initial_ticks), last_ticks_(initial_ticks),
      committed_guest_ticks_(initial_ticks) {
    if (source_.read == nullptr || source_.frequency == 0 ||
        source_.frequency > kMaximumSupportedCounterFrequency) {
        throw std::invalid_argument("invalid runtime timeline counter source");
    }
    epoch_counter_ = source_.read(source_.user);
    last_counter_.store(epoch_counter_, std::memory_order_release);
}

std::uint64_t RuntimeTimeline::now_ticks(std::uint64_t* sampled_counter) noexcept {
    const std::uint64_t observed = source_.read(source_.user);
    if (sampled_counter != nullptr) {
        *sampled_counter = observed;
    }
    std::uint64_t monotonic_counter =
        last_counter_.load(std::memory_order_acquire);
    while (observed > monotonic_counter &&
           !last_counter_.compare_exchange_weak(
               monotonic_counter,
               observed,
               std::memory_order_acq_rel,
               std::memory_order_acquire)) {
    }
    if (observed > monotonic_counter) {
        monotonic_counter = observed;
    }

    const std::uint64_t delta =
        monotonic_counter >= epoch_counter_
            ? monotonic_counter - epoch_counter_
            : 0;
    bool conversion_saturated = false;
    const std::uint64_t scaled = scale_counter_delta(
        delta, source_.frequency, &conversion_saturated);
    bool add_saturated = conversion_saturated;
    const std::uint64_t host_candidate =
        saturating_add(epoch_ticks_, scaled, add_saturated);
    if (add_saturated) {
        saturated_.store(true, std::memory_order_release);
    }
    const std::uint64_t discarded =
        discarded_host_ticks_.load(std::memory_order_acquire);
    const std::uint64_t candidate = host_candidate >= discarded
        ? host_candidate - discarded
        : 0u;

    std::uint64_t last = last_ticks_.load(std::memory_order_acquire);
    while (candidate > last &&
           !last_ticks_.compare_exchange_weak(
               last,
               candidate,
               std::memory_order_acq_rel,
               std::memory_order_acquire)) {
    }
    return candidate > last ? candidate : last;
}

void RuntimeTimeline::commit_guest_observation(std::uint64_t ticks) {
    std::uint64_t committed =
        committed_guest_ticks_.load(std::memory_order_acquire);
    while (ticks > committed &&
           !committed_guest_ticks_.compare_exchange_weak(
               committed, ticks, std::memory_order_acq_rel,
               std::memory_order_acquire)) {
    }
    if (ticks < committed) {
        throw std::runtime_error(
            "runtime timeline guest/device observation moved backward");
    }
}

std::optional<std::uint64_t>
RuntimeTimeline::host_suspension_discarded_ticks(
    std::uint64_t last_guest_ticks,
    std::uint64_t observed_host_ticks) const noexcept {
    if (last_guest_ticks > observed_host_ticks || saturated() ||
        last_guest_ticks < committed_guest_ticks()) {
        return std::nullopt;
    }

    const std::uint64_t discarded =
        discarded_host_ticks_.load(std::memory_order_acquire);
    bool overflowed = false;
    const std::uint64_t unrebased_observed = saturating_add(
        observed_host_ticks, discarded, overflowed);
    if (overflowed || unrebased_observed < last_guest_ticks) {
        return std::nullopt;
    }

    const std::uint64_t new_discarded =
        unrebased_observed - last_guest_ticks;
    if (new_discarded < discarded) {
        return std::nullopt;
    }
    return new_discarded;
}

bool RuntimeTimeline::can_rebase_after_host_suspension(
    std::uint64_t last_guest_ticks,
    std::uint64_t observed_host_ticks) const noexcept {
    return host_suspension_discarded_ticks(
        last_guest_ticks, observed_host_ticks).has_value();
}

bool RuntimeTimeline::rebase_after_host_suspension(
    std::uint64_t last_guest_ticks,
    std::uint64_t observed_host_ticks) noexcept {
    const auto new_discarded = host_suspension_discarded_ticks(
        last_guest_ticks, observed_host_ticks);
    if (!new_discarded.has_value()) {
        return false;
    }

    // DeadlineBroker holds its worker mutex while calling this function.  The
    // runtime invokes it only from the simulation thread, so no timeline read
    // can publish a stale high-water mark after this reset.
    discarded_host_ticks_.store(*new_discarded, std::memory_order_release);
    last_ticks_.store(last_guest_ticks, std::memory_order_release);
    return true;
}

PeriodicDeadline::PeriodicDeadline(
    std::uint64_t first_deadline_ticks,
    std::uint64_t period_ticks,
    std::uint64_t first_sequence)
    : next_deadline_ticks_(first_deadline_ticks),
      period_ticks_(period_ticks),
      next_sequence_(first_sequence) {
    if (period_ticks_ == 0 || next_sequence_ == 0) {
        throw std::invalid_argument("invalid periodic deadline");
    }
}

std::vector<DeadlineEvent> PeriodicDeadline::collect_due(
    EventKind kind,
    std::uint64_t now_ticks,
    std::size_t maximum_events) {
    if (static_cast<std::size_t>(kind) >= kEventKindCount) {
        throw std::invalid_argument("invalid deadline event kind");
    }
    if (now_ticks < next_deadline_ticks_) {
        return {};
    }

    const std::uint64_t due_count_u64 = checked_add(
        (now_ticks - next_deadline_ticks_) / period_ticks_, 1u,
        "periodic deadline backlog overflow");
    if (due_count_u64 > maximum_events ||
        due_count_u64 > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("periodic deadline backlog exceeds limit");
    }
    const auto due_count = static_cast<std::size_t>(due_count_u64);
    std::vector<DeadlineEvent> events;
    events.reserve(due_count);
    for (std::size_t index = 0; index < due_count; ++index) {
        const std::uint64_t offset = checked_multiply(
            static_cast<std::uint64_t>(index),
            period_ticks_,
            "periodic deadline offset overflow");
        events.push_back(DeadlineEvent{
            kind,
            checked_add(
                next_sequence_,
                static_cast<std::uint64_t>(index),
                "periodic deadline sequence overflow"),
            checked_add(
                next_deadline_ticks_,
                offset,
                "periodic deadline tick overflow")});
    }

    const std::uint64_t advance = checked_multiply(
        due_count_u64,
        period_ticks_,
        "periodic deadline advance overflow");
    const std::uint64_t next_deadline = checked_add(
        next_deadline_ticks_, advance, "periodic deadline tick overflow");
    const std::uint64_t next_sequence = checked_add(
        next_sequence_, due_count_u64, "periodic deadline sequence overflow");
    next_deadline_ticks_ = next_deadline;
    next_sequence_ = next_sequence;
    return events;
}

std::size_t DeadlineSchedule::index(EventKind kind) {
    const auto value = static_cast<std::size_t>(kind);
    if (value >= kEventKindCount) {
        throw std::invalid_argument("invalid deadline event kind");
    }
    return value;
}

void DeadlineSchedule::set_periodic(
    EventKind kind,
    std::uint64_t first_deadline_ticks,
    std::uint64_t period_ticks,
    std::uint64_t first_sequence) {
    const std::size_t slot = index(kind);
    if (std::any_of(one_shots_.begin(), one_shots_.end(),
            [kind](const DeadlineEvent& event) {
                return event.kind == kind && event.replaceable;
            })) {
        throw std::logic_error(
            "event kind cannot be both periodic and replaceable");
    }
    const PeriodicDeadline candidate(
        first_deadline_ticks, period_ticks, first_sequence);
    periodic_[slot] = candidate;
}

void DeadlineSchedule::cancel_periodic(EventKind kind) noexcept {
    const auto value = static_cast<std::size_t>(kind);
    if (value < kEventKindCount) {
        periodic_[value].reset();
    }
}

void DeadlineSchedule::schedule_once(
    EventKind kind,
    std::uint64_t deadline_ticks,
    std::optional<std::uint64_t> sequence) {
    const std::size_t slot = index(kind);
    if (std::any_of(one_shots_.begin(), one_shots_.end(),
            [kind](const DeadlineEvent& event) {
                return event.kind == kind && event.replaceable;
            })) {
        throw std::logic_error(
            "event kind cannot mix counted and replaceable one-shots");
    }
    const std::uint64_t event_sequence = sequence.value_or(0u) == 0u
        ? checked_add(next_one_shot_sequence_[slot], 1u,
              "one-shot deadline sequence overflow")
        : *sequence;
    one_shots_.push_back(DeadlineEvent{kind, event_sequence, deadline_ticks});
    next_one_shot_sequence_[slot] =
        std::max(next_one_shot_sequence_[slot], event_sequence);
}

void DeadlineSchedule::replace_once(
    EventKind kind,
    std::uint64_t deadline_ticks,
    std::uint64_t generation) {
    const std::size_t slot = index(kind);
    if (generation == 0u ||
        generation <= latest_replaceable_generation_[slot]) {
        throw std::invalid_argument(
            "replaceable deadline generation must increase");
    }
    if (periodic_[slot].has_value()) {
        throw std::logic_error(
            "event kind cannot be both periodic and replaceable");
    }
    const bool has_counted_one_shot = std::any_of(
        one_shots_.begin(),
        one_shots_.end(),
        [kind](const DeadlineEvent& event) {
            return event.kind == kind && !event.replaceable;
        });
    if (has_counted_one_shot) {
        throw std::logic_error(
            "event kind cannot mix counted and replaceable one-shots");
    }

    // Acquire capacity before retiring the previous owner's event.
    one_shots_.reserve(one_shots_.size() + 1u);
    std::erase_if(one_shots_, [kind](const DeadlineEvent& event) {
        return event.kind == kind && event.replaceable;
    });
    one_shots_.push_back(
        DeadlineEvent{kind, generation, deadline_ticks, true});
    latest_replaceable_generation_[slot] = generation;
}

void DeadlineSchedule::restore_replaceable_after_host_suspension(
    EventKind kind,
    std::uint64_t deadline_ticks,
    std::uint64_t generation) {
    const std::size_t slot = index(kind);
    if (generation == 0u ||
        generation != latest_replaceable_generation_[slot]) {
        throw std::invalid_argument(
            "restored replaceable deadline generation is not the current owner");
    }
    if (periodic_[slot].has_value()) {
        throw std::logic_error(
            "event kind cannot be both periodic and replaceable");
    }
    const bool has_counted_one_shot = std::any_of(
        one_shots_.begin(),
        one_shots_.end(),
        [kind](const DeadlineEvent& event) {
            return event.kind == kind && !event.replaceable;
        });
    if (has_counted_one_shot) {
        throw std::logic_error(
            "event kind cannot mix counted and replaceable one-shots");
    }

    one_shots_.reserve(one_shots_.size() + 1u);
    std::erase_if(one_shots_, [kind](const DeadlineEvent& event) {
        return event.kind == kind && event.replaceable;
    });
    one_shots_.push_back(
        DeadlineEvent{kind, generation, deadline_ticks, true});
}

void DeadlineSchedule::cancel_once(
    EventKind kind,
    std::uint64_t generation) {
    const std::size_t slot = index(kind);
    if (generation == 0u ||
        generation <= latest_replaceable_generation_[slot]) {
        throw std::invalid_argument(
            "replaceable cancellation generation must increase");
    }
    std::erase_if(one_shots_, [kind](const DeadlineEvent& event) {
        return event.kind == kind && event.replaceable;
    });
    latest_replaceable_generation_[slot] = generation;
}

std::optional<std::uint64_t> DeadlineSchedule::next_deadline_ticks() const {
    std::optional<std::uint64_t> next;
    for (const auto& periodic : periodic_) {
        if (periodic.has_value()) {
            next = next.has_value()
                ? std::min(*next, periodic->next_deadline_ticks())
                : periodic->next_deadline_ticks();
        }
    }
    for (const DeadlineEvent& event : one_shots_) {
        next = next.has_value()
            ? std::min(*next, event.deadline_ticks)
            : event.deadline_ticks;
    }
    return next;
}

std::vector<DeadlineEvent> DeadlineSchedule::collect_due(
    std::uint64_t now_ticks,
    std::size_t maximum_events) {
    std::vector<DeadlineEvent> due;
    for (std::size_t slot = 0; slot < periodic_.size(); ++slot) {
        if (!periodic_[slot].has_value()) {
            continue;
        }
        std::vector<DeadlineEvent> periodic_due =
            periodic_[slot]->collect_due(
                static_cast<EventKind>(slot),
                now_ticks,
                maximum_events - std::min(maximum_events, due.size()));
        if (periodic_due.size() > maximum_events - due.size()) {
            throw std::overflow_error("deadline backlog exceeds limit");
        }
        due.insert(
            due.end(),
            std::make_move_iterator(periodic_due.begin()),
            std::make_move_iterator(periodic_due.end()));
    }

    auto event = one_shots_.begin();
    while (event != one_shots_.end()) {
        if (event->deadline_ticks > now_ticks) {
            ++event;
            continue;
        }
        if (due.size() >= maximum_events) {
            throw std::overflow_error("deadline backlog exceeds limit");
        }
        due.push_back(*event);
        event = one_shots_.erase(event);
    }
    std::sort(due.begin(), due.end(), event_less);
    return due;
}

std::size_t PendingEventLatch::index(EventKind kind) noexcept {
    const auto value = static_cast<std::size_t>(kind);
    return value < kEventKindCount ? value : kEventKindCount;
}

bool PendingEventLatch::publish(EventKind kind, std::uint64_t count) noexcept {
    const std::size_t slot = index(kind);
    if (slot == kEventKindCount || count == 0) {
        return count == 0 && slot != kEventKindCount;
    }
    lock_slot(slot);
    const std::uint64_t current =
        counts_[slot].load(std::memory_order_relaxed);
    if (periodic_batches_[slot].count != 0u) {
        unlock_slot(slot);
        return false;
    }
    if (count > std::numeric_limits<std::uint64_t>::max() - current) {
        unlock_slot(slot);
        return false;
    }
    counts_[slot].store(current + count, std::memory_order_release);
    mask_.fetch_or(event_bit(kind), std::memory_order_release);
    unlock_slot(slot);
    return true;
}

bool PendingEventLatch::publish_periodic(
    const DeadlineEvent& event) noexcept {
    const std::size_t slot = index(event.kind);
    if (slot == kEventKindCount || event.replaceable ||
        event.sequence == 0u) {
        return false;
    }

    lock_slot(slot);
    const std::uint64_t current =
        counts_[slot].load(std::memory_order_relaxed);
    PeriodicBatch& batch = periodic_batches_[slot];
    const std::uint64_t established_period = periodic_period_ticks_[slot];
    const std::uint64_t deadline_step =
        current == 0u ? 0u : event.deadline_ticks - batch.last_deadline_ticks;
    if (current == std::numeric_limits<std::uint64_t>::max() ||
        (current == 0u &&
         (batch.count != 0u || established_period != 0u)) ||
        (current != 0u &&
         (!batch.has_identity() || batch.count != current ||
          batch.last_sequence == std::numeric_limits<std::uint64_t>::max() ||
          event.sequence != batch.last_sequence + 1u ||
          event.deadline_ticks <= batch.last_deadline_ticks ||
          (established_period != 0u &&
           deadline_step != established_period)))) {
        unlock_slot(slot);
        return false;
    }

    if (current == 0u) {
        batch = PeriodicBatch{
            1u,
            event.sequence,
            event.sequence,
            event.deadline_ticks,
            event.deadline_ticks};
        periodic_period_ticks_[slot] = 0u;
    } else {
        if (established_period == 0u) {
            periodic_period_ticks_[slot] = deadline_step;
        }
        ++batch.count;
        batch.last_sequence = event.sequence;
        batch.last_deadline_ticks = event.deadline_ticks;
    }
    counts_[slot].store(current + 1u, std::memory_order_release);
    mask_.fetch_or(event_bit(event.kind), std::memory_order_release);
    unlock_slot(slot);
    return true;
}

bool PendingEventLatch::publish_replaceable(
    EventKind kind,
    std::uint64_t generation) noexcept {
    const std::size_t slot = index(kind);
    if (slot == kEventKindCount || generation == 0u) {
        return false;
    }
    lock_slot(slot);
    if (replaceable_generations_[slot].load(std::memory_order_relaxed) != 0u) {
        unlock_slot(slot);
        return false;
    }
    replaceable_generations_[slot].store(
        generation, std::memory_order_release);
    mask_.fetch_or(event_bit(kind), std::memory_order_release);
    unlock_slot(slot);
    return true;
}

std::uint64_t PendingEventLatch::pending(EventKind kind) const noexcept {
    const std::size_t slot = index(kind);
    return slot == kEventKindCount
        ? 0
        : counts_[slot].load(std::memory_order_acquire);
}

std::uint64_t PendingEventLatch::pending_replaceable_generation(
    EventKind kind) const noexcept {
    const std::size_t slot = index(kind);
    return slot == kEventKindCount
        ? 0u
        : replaceable_generations_[slot].load(std::memory_order_acquire);
}

PendingEventLatch::LockContentionStats
PendingEventLatch::lock_contention_stats(EventKind kind) const noexcept {
    const std::size_t slot = index(kind);
    if (slot == kEventKindCount) {
        return {};
    }
    return LockContentionStats{
        contended_acquisitions_[slot].load(std::memory_order_relaxed),
        total_spins_[slot].load(std::memory_order_relaxed),
        max_spins_[slot].load(std::memory_order_relaxed)};
}

void PendingEventLatch::lock_slot(std::size_t slot) noexcept {
    std::uint64_t spins = 0u;
    while (slot_locks_[slot].test_and_set(std::memory_order_acquire)) {
        if (spins != std::numeric_limits<std::uint64_t>::max()) {
            ++spins;
        }
        // Every holder of this slot lock does only a handful of atomic
        // operations before releasing it (see the lock_slot/unlock_slot pairs
        // above), so contention is short by construction and the wait should be
        // a true local spin. `std::this_thread::yield()` is the wrong primitive
        // for that: it hands the remainder of the time slice to another runnable
        // thread and costs a ring transition, which on a loaded CPU is far more
        // than the critical section it is waiting for. That matters here because
        // these slots back the VI, input, audio and DSP deadline edges.
        //
        // A bounded pause-spin covers the ordinary short hold without ever
        // entering the scheduler; only a genuinely long hold falls through to
        // yield, which keeps the pathological case from burning a core.
        constexpr std::uint64_t kPauseSpins = 1024u;
        if (spins <= kPauseSpins) {
#if defined(_M_X64) || defined(_M_IX86)
            _mm_pause();
#else
            std::this_thread::yield();
#endif
        } else {
            std::this_thread::yield();
        }
    }
    if (spins == 0u) {
        return;
    }
    saturating_atomic_add(contended_acquisitions_[slot], 1u);
    saturating_atomic_add(total_spins_[slot], spins);
    update_atomic_max(max_spins_[slot], spins);
}

void PendingEventLatch::unlock_slot(std::size_t slot) noexcept {
    slot_locks_[slot].clear(std::memory_order_release);
}

void PendingEventLatch::repair_mask_while_locked(
    std::size_t slot,
    EventKind kind) noexcept {
    const std::uint32_t bit = event_bit(kind);
    if (counts_[slot].load(std::memory_order_relaxed) != 0u ||
        replaceable_generations_[slot].load(std::memory_order_relaxed) != 0u) {
        mask_.fetch_or(bit, std::memory_order_release);
    } else {
        mask_.fetch_and(~bit, std::memory_order_release);
    }
}

void PendingEventLatch::clear_periodic_identity_while_locked(
    std::size_t slot) noexcept {
    periodic_batches_[slot] = {};
    periodic_period_ticks_[slot] = 0u;
}

bool PendingEventLatch::take_one(EventKind kind) noexcept {
    const std::size_t slot = index(kind);
    if (slot == kEventKindCount) {
        return false;
    }
    lock_slot(slot);
    const std::uint64_t current =
        counts_[slot].load(std::memory_order_relaxed);
    if (current == 0u) {
        repair_mask_while_locked(slot, kind);
        unlock_slot(slot);
        return false;
    }
    counts_[slot].store(current - 1u, std::memory_order_release);
    if (periodic_batches_[slot].has_identity()) {
        PeriodicBatch& batch = periodic_batches_[slot];
        if (batch.count != current || batch.first_sequence == 0u ||
            batch.last_sequence < batch.first_sequence ||
            batch.last_sequence - batch.first_sequence != current - 1u ||
            batch.last_deadline_ticks < batch.first_deadline_ticks) {
            clear_periodic_identity_while_locked(slot);
        } else if (current == 1u) {
            clear_periodic_identity_while_locked(slot);
        } else {
            const std::uint64_t deadline_span =
                batch.last_deadline_ticks - batch.first_deadline_ticks;
            const std::uint64_t intervals = current - 1u;
            if (deadline_span % intervals != 0u) {
                clear_periodic_identity_while_locked(slot);
            } else {
                ++batch.first_sequence;
                batch.first_deadline_ticks += deadline_span / intervals;
                --batch.count;
            }
        }
    }
    if (current == 1u) {
        repair_mask_while_locked(slot, kind);
    }
    unlock_slot(slot);
    return true;
}

std::uint64_t PendingEventLatch::take_all(EventKind kind) noexcept {
    const std::size_t slot = index(kind);
    if (slot == kEventKindCount) {
        return 0;
    }
    lock_slot(slot);
    const std::uint64_t count = counts_[slot].exchange(
        0u, std::memory_order_acq_rel);
    clear_periodic_identity_while_locked(slot);
    repair_mask_while_locked(slot, kind);
    unlock_slot(slot);
    return count;
}

PendingEventLatch::PeriodicBatch PendingEventLatch::take_all_periodic(
    EventKind kind) noexcept {
    const std::size_t slot = index(kind);
    if (slot == kEventKindCount) {
        return {};
    }
    lock_slot(slot);
    const std::uint64_t count = counts_[slot].exchange(
        0u, std::memory_order_acq_rel);
    PeriodicBatch batch = periodic_batches_[slot];
    clear_periodic_identity_while_locked(slot);
    if (batch.count != count) {
        batch = {};
        batch.count = count;
    }
    repair_mask_while_locked(slot, kind);
    unlock_slot(slot);
    return batch;
}

PendingEventLatch::PeriodicPrefixTakeResult
PendingEventLatch::take_periodic_prefix_if_matches(
    EventKind kind,
    std::uint64_t expected_first_sequence,
    std::uint64_t expected_first_deadline_ticks,
    std::uint64_t period_ticks,
    std::uint64_t due_count) noexcept {
    const std::size_t slot = index(kind);
    if (slot == kEventKindCount || expected_first_sequence == 0u ||
        period_ticks == 0u || due_count == 0u) {
        return PeriodicPrefixTakeResult{
            PeriodicPrefixTakeStatus::Invalid, {}, 0u};
    }

    lock_slot(slot);
    const std::uint64_t current =
        counts_[slot].load(std::memory_order_relaxed);
    if (current == 0u) {
        repair_mask_while_locked(slot, kind);
        unlock_slot(slot);
        return {};
    }
    const PeriodicBatch current_batch = periodic_batches_[slot];
    const std::uint64_t established_period = periodic_period_ticks_[slot];
    const std::uint64_t current_intervals = current - 1u;
    const bool sequence_overflow =
        current_intervals >
        std::numeric_limits<std::uint64_t>::max() -
            current_batch.first_sequence;
    const bool deadline_multiply_overflow =
        current_intervals != 0u &&
        period_ticks >
            std::numeric_limits<std::uint64_t>::max() / current_intervals;
    const std::uint64_t current_deadline_span =
        deadline_multiply_overflow ? 0u : current_intervals * period_ticks;
    const bool deadline_add_overflow =
        !deadline_multiply_overflow &&
        current_deadline_span >
            std::numeric_limits<std::uint64_t>::max() -
                current_batch.first_deadline_ticks;
    if (sequence_overflow || deadline_multiply_overflow ||
        deadline_add_overflow) {
        unlock_slot(slot);
        return PeriodicPrefixTakeResult{
            PeriodicPrefixTakeStatus::ArithmeticOverflow, {}, current};
    }
    const bool period_matches = current == 1u
        ? established_period == 0u || established_period == period_ticks
        : established_period == period_ticks;
    if (!current_batch.has_identity() || current_batch.count != current ||
        !period_matches ||
        current_batch.last_sequence !=
            current_batch.first_sequence + current_intervals ||
        current_batch.last_deadline_ticks !=
            current_batch.first_deadline_ticks + current_deadline_span ||
        current_batch.first_sequence != expected_first_sequence ||
        current_batch.first_deadline_ticks !=
            expected_first_deadline_ticks) {
        unlock_slot(slot);
        return PeriodicPrefixTakeResult{
            PeriodicPrefixTakeStatus::Invalid, {}, current};
    }

    if (current < due_count) {
        unlock_slot(slot);
        return PeriodicPrefixTakeResult{
            PeriodicPrefixTakeStatus::Incomplete, {}, current};
    }

    const std::uint64_t prefix_intervals = due_count - 1u;
    if (prefix_intervals >
            std::numeric_limits<std::uint64_t>::max() -
                expected_first_sequence ||
        (prefix_intervals != 0u &&
         period_ticks >
             std::numeric_limits<std::uint64_t>::max() / prefix_intervals)) {
        unlock_slot(slot);
        return PeriodicPrefixTakeResult{
            PeriodicPrefixTakeStatus::ArithmeticOverflow, {}, current};
    }
    const std::uint64_t prefix_deadline_span =
        prefix_intervals * period_ticks;
    if (prefix_deadline_span >
        std::numeric_limits<std::uint64_t>::max() -
            expected_first_deadline_ticks) {
        unlock_slot(slot);
        return PeriodicPrefixTakeResult{
            PeriodicPrefixTakeStatus::ArithmeticOverflow, {}, current};
    }
    const PeriodicBatch taken{
        due_count,
        expected_first_sequence,
        expected_first_sequence + prefix_intervals,
        expected_first_deadline_ticks,
        expected_first_deadline_ticks + prefix_deadline_span};

    if (current == due_count) {
        counts_[slot].store(0u, std::memory_order_release);
        clear_periodic_identity_while_locked(slot);
    } else {
        // current > due_count, so due_count is no larger than the already
        // overflow-checked current_intervals value above.
        const std::uint64_t suffix_deadline_span = due_count * period_ticks;
        PeriodicBatch& suffix = periodic_batches_[slot];
        suffix.count = current - due_count;
        suffix.first_sequence += due_count;
        suffix.first_deadline_ticks += suffix_deadline_span;
        counts_[slot].store(current - due_count, std::memory_order_release);
    }
    repair_mask_while_locked(slot, kind);
    unlock_slot(slot);
    return PeriodicPrefixTakeResult{
        PeriodicPrefixTakeStatus::Taken, taken, current};
}

PendingEventLatch::ExactTakeResult PendingEventLatch::take_one_if_only(
    EventKind kind) noexcept {
    const std::size_t slot = index(kind);
    if (slot == kEventKindCount) {
        return {};
    }

    lock_slot(slot);
    const std::uint64_t current =
        counts_[slot].load(std::memory_order_relaxed);
    if (current == 0u) {
        repair_mask_while_locked(slot, kind);
        unlock_slot(slot);
        return {};
    }
    if (current > 1u) {
        unlock_slot(slot);
        return ExactTakeResult{ExactTakeStatus::Backlog, current};
    }
    counts_[slot].store(0u, std::memory_order_release);
    clear_periodic_identity_while_locked(slot);
    repair_mask_while_locked(slot, kind);
    unlock_slot(slot);
    return ExactTakeResult{ExactTakeStatus::Taken, 1u};
}

PendingEventLatch::ReplaceableTakeResult
PendingEventLatch::take_replaceable(
    EventKind kind,
    std::uint64_t expected_generation) noexcept {
    const std::size_t slot = index(kind);
    if (slot == kEventKindCount) {
        return {};
    }

    lock_slot(slot);
    const std::uint64_t observed =
        replaceable_generations_[slot].load(std::memory_order_relaxed);
    if (observed == 0u) {
        repair_mask_while_locked(slot, kind);
        unlock_slot(slot);
        return {};
    }
    if (observed > expected_generation) {
        unlock_slot(slot);
        return ReplaceableTakeResult{
            ReplaceableTakeStatus::Future, observed};
    }
    replaceable_generations_[slot].store(0u, std::memory_order_release);
    repair_mask_while_locked(slot, kind);
    unlock_slot(slot);
    return ReplaceableTakeResult{
        observed == expected_generation
            ? ReplaceableTakeStatus::Taken
            : ReplaceableTakeStatus::Stale,
        observed};
}

void PendingEventLatch::clear_replaceable(EventKind kind) noexcept {
    const std::size_t slot = index(kind);
    if (slot == kEventKindCount) {
        return;
    }
    lock_slot(slot);
    replaceable_generations_[slot].store(0u, std::memory_order_release);
    repair_mask_while_locked(slot, kind);
    unlock_slot(slot);
}

ExactPeriodicDeadlineConsumer::ExactPeriodicDeadlineConsumer(
    std::uint64_t first_deadline_ticks,
    std::uint64_t period_ticks,
    std::uint64_t first_sequence)
    : first_deadline_ticks_(first_deadline_ticks),
      period_ticks_(period_ticks),
      first_sequence_(first_sequence) {
    if (period_ticks_ == 0 || first_sequence_ == 0) {
        throw std::invalid_argument("invalid exact periodic deadline consumer");
    }
}

PeriodicDeadlineObservation ExactPeriodicDeadlineConsumer::inspect_count(
    std::uint64_t pending_edges,
    std::uint64_t now_ticks) const noexcept {
    PeriodicDeadlineObservation observation{};
    observation.pending_edges = pending_edges;

    const auto multiply_without_overflow = [](
        std::uint64_t lhs,
        std::uint64_t rhs,
        std::uint64_t& result) noexcept {
        if (lhs != 0 &&
            rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
            return false;
        }
        result = lhs * rhs;
        return true;
    };
    const auto add_without_overflow = [](
        std::uint64_t lhs,
        std::uint64_t rhs,
        std::uint64_t& result) noexcept {
        if (rhs > std::numeric_limits<std::uint64_t>::max() - lhs) {
            return false;
        }
        result = lhs + rhs;
        return true;
    };

    std::uint64_t deadline_offset = 0;
    if (!multiply_without_overflow(
            delivered_edges_, period_ticks_, deadline_offset) ||
        !add_without_overflow(
            first_deadline_ticks_,
            deadline_offset,
            observation.expected_deadline_ticks) ||
        !add_without_overflow(
            first_sequence_,
            delivered_edges_,
            observation.expected_sequence)) {
        observation.status = PeriodicConsumeStatus::ArithmeticOverflow;
        return observation;
    }

    if (now_ticks < observation.expected_deadline_ticks) {
        if (pending_edges == 0) {
            observation.status = PeriodicConsumeStatus::NotDue;
            return observation;
        }
        observation.status = PeriodicConsumeStatus::EarlyPublication;
        observation.missed_or_coalesced_edges = pending_edges - 1u;
        return observation;
    }

    const std::uint64_t elapsed_periods =
        (now_ticks - observation.expected_deadline_ticks) / period_ticks_;
    if (elapsed_periods == std::numeric_limits<std::uint64_t>::max()) {
        observation.status = PeriodicConsumeStatus::ArithmeticOverflow;
        return observation;
    }
    observation.scheduled_due_edges = elapsed_periods + 1u;
    observation.lateness_ticks =
        now_ticks - observation.expected_deadline_ticks;
    if (!add_without_overflow(
            delivered_edges_,
            observation.scheduled_due_edges,
            observation.scheduled_edges_elapsed)) {
        observation.status = PeriodicConsumeStatus::ArithmeticOverflow;
        return observation;
    }

    const std::uint64_t represented_edges =
        std::max(pending_edges, observation.scheduled_due_edges);
    observation.missed_or_coalesced_edges = represented_edges - 1u;
    if (represented_edges > 1u) {
        observation.status = PeriodicConsumeStatus::Backlog;
    } else if (pending_edges == 0) {
        observation.status = PeriodicConsumeStatus::AwaitingPublication;
    } else {
        observation.status = PeriodicConsumeStatus::Ready;
    }
    return observation;
}

PeriodicDeadlineObservation ExactPeriodicDeadlineConsumer::inspect(
    const PendingEventLatch& latch,
    EventKind kind,
    std::uint64_t now_ticks) const noexcept {
    return inspect_count(latch.pending(kind), now_ticks);
}

void ExactPeriodicDeadlineConsumer::account_observation(
    const PeriodicDeadlineObservation& observation) noexcept {
    stats_.max_pending_backlog =
        std::max(stats_.max_pending_backlog, observation.pending_edges);
    stats_.max_lateness_ticks =
        std::max(stats_.max_lateness_ticks, observation.lateness_ticks);
    stats_.scheduled_edges =
        std::max(stats_.scheduled_edges, observation.scheduled_edges_elapsed);

    if (observation.scheduled_edges_elapsed != 0 &&
        observation.expected_sequence != 0) {
        const std::uint64_t last_offset =
            observation.scheduled_due_edges - 1u;
        if (last_offset <=
            std::numeric_limits<std::uint64_t>::max() -
                observation.expected_sequence) {
            stats_.last_scheduled_sequence =
                observation.expected_sequence + last_offset;
        }
    }
}

[[noreturn]] void ExactPeriodicDeadlineConsumer::fail(
    const PeriodicDeadlineObservation& observation,
    const char* message,
    bool arithmetic_failure) {
    account_observation(observation);
    if (stats_.locked_rate_valid) {
        stats_.locked_rate_valid = false;
        stats_.missed_or_coalesced_edges =
            observation.missed_or_coalesced_edges;
        if (observation.status == PeriodicConsumeStatus::EarlyPublication) {
            stats_.early_publication_edges = observation.pending_edges;
        }
        if (arithmetic_failure) {
            stats_.arithmetic_failures = 1u;
        }
    }
    if (arithmetic_failure) {
        throw std::overflow_error(message);
    }
    throw std::runtime_error(message);
}

std::optional<DeadlineEvent>
ExactPeriodicDeadlineConsumer::consume_one_or_throw(
    PendingEventLatch& latch,
    EventKind kind,
    std::uint64_t now_ticks) {
    if (!stats_.locked_rate_valid) {
        throw std::runtime_error(
            "exact periodic deadline consumer is already invalid");
    }

    PeriodicDeadlineObservation observation = inspect(latch, kind, now_ticks);
    switch (observation.status) {
        case PeriodicConsumeStatus::NotDue:
        case PeriodicConsumeStatus::AwaitingPublication:
            account_observation(observation);
            return std::nullopt;
        case PeriodicConsumeStatus::EarlyPublication:
            fail(
                observation,
                "periodic deadline was published before its exact deadline",
                false);
        case PeriodicConsumeStatus::Backlog:
            fail(
                observation,
                "periodic deadline backlog invalidates locked cadence",
                false);
        case PeriodicConsumeStatus::ArithmeticOverflow:
            fail(
                observation,
                "periodic deadline audit arithmetic overflow",
                true);
        case PeriodicConsumeStatus::Ready:
            break;
    }

    const PendingEventLatch::ExactTakeResult take =
        latch.take_one_if_only(kind);
    if (take.status == PendingEventLatch::ExactTakeStatus::Empty) {
        // Publishers only increase the count. Reaching zero after observing one
        // therefore proves an unsupported second consumer raced this policy.
        fail(
            observation,
            "periodic deadline was consumed by a competing consumer",
            false);
    }
    if (take.status == PendingEventLatch::ExactTakeStatus::Backlog) {
        observation = inspect_count(take.observed_count, now_ticks);
        fail(
            observation,
            "concurrent publication created a periodic deadline backlog",
            false);
    }

    account_observation(observation);
    const DeadlineEvent event{
        kind,
        observation.expected_sequence,
        observation.expected_deadline_ticks};
    ++delivered_edges_;
    stats_.delivered_edges = delivered_edges_;
    ++stats_.delivered_interrupts;
    stats_.last_delivered_sequence = event.sequence;
    return event;
}

std::optional<DeadlineEvent>
ExactPeriodicDeadlineConsumer::consume_one_or_coalesce_level_or_throw(
    PendingEventLatch& latch,
    EventKind kind,
    std::uint64_t now_ticks) {
    if (!stats_.locked_rate_valid) {
        throw std::runtime_error(
            "exact periodic deadline consumer is already invalid");
    }

    PeriodicDeadlineObservation observation = inspect(latch, kind, now_ticks);
    switch (observation.status) {
        case PeriodicConsumeStatus::NotDue:
        case PeriodicConsumeStatus::AwaitingPublication:
            account_observation(observation);
            return std::nullopt;
        case PeriodicConsumeStatus::EarlyPublication:
            fail(
                observation,
                "periodic deadline was published before its exact deadline",
                false);
        case PeriodicConsumeStatus::ArithmeticOverflow:
            fail(
                observation,
                "periodic deadline audit arithmetic overflow",
                true);
        case PeriodicConsumeStatus::Ready:
        case PeriodicConsumeStatus::Backlog:
            break;
    }

    const std::uint64_t due_count = observation.scheduled_due_edges;
    const PendingEventLatch::PeriodicPrefixTakeResult take =
        latch.take_periodic_prefix_if_matches(
            kind,
            observation.expected_sequence,
            observation.expected_deadline_ticks,
            period_ticks_,
            due_count);
    if (take.status ==
        PendingEventLatch::PeriodicPrefixTakeStatus::Empty) {
        fail(
            observation,
            "periodic deadline was consumed by a competing consumer",
            false);
    }
    if (take.status ==
        PendingEventLatch::PeriodicPrefixTakeStatus::Incomplete) {
        if (observation.pending_edges < due_count &&
            take.observed_count >= observation.pending_edges) {
            account_observation(observation);
            return std::nullopt;
        }
        fail(
            observation,
            "periodic deadline prefix was consumed by a competing consumer",
            false);
    }
    if (take.status ==
        PendingEventLatch::PeriodicPrefixTakeStatus::ArithmeticOverflow) {
        fail(
            observation,
            "level-triggered periodic batch identity overflow",
            true);
    }
    if (take.status ==
        PendingEventLatch::PeriodicPrefixTakeStatus::Invalid) {
        fail(
            observation,
            "level-triggered periodic backlog lacks exact batch identity",
            false);
    }

    const PendingEventLatch::PeriodicBatch& batch = take.batch;
    observation = inspect_count(batch.count, now_ticks);
    if (observation.status == PeriodicConsumeStatus::ArithmeticOverflow) {
        fail(
            observation,
            "level-triggered periodic batch arithmetic overflow",
            true);
    }
    if ((observation.status != PeriodicConsumeStatus::Ready &&
         observation.status != PeriodicConsumeStatus::Backlog) ||
        batch.count != observation.scheduled_due_edges ||
        batch.first_sequence != observation.expected_sequence ||
        batch.first_deadline_ticks != observation.expected_deadline_ticks) {
        fail(
            observation,
            "level-triggered periodic backlog does not match the due schedule",
            false);
    }

    std::uint64_t sequence_offset = batch.count - 1u;
    std::uint64_t deadline_offset = 0u;
    if (sequence_offset >
            std::numeric_limits<std::uint64_t>::max() -
                observation.expected_sequence ||
        (sequence_offset != 0u &&
            period_ticks_ >
                std::numeric_limits<std::uint64_t>::max() / sequence_offset)) {
        fail(
            observation,
            "level-triggered periodic batch identity overflow",
            true);
    }
    deadline_offset = sequence_offset * period_ticks_;
    if (deadline_offset >
        std::numeric_limits<std::uint64_t>::max() -
            observation.expected_deadline_ticks) {
        fail(
            observation,
            "level-triggered periodic batch deadline overflow",
            true);
    }
    const std::uint64_t expected_last_sequence =
        observation.expected_sequence + sequence_offset;
    const std::uint64_t expected_last_deadline =
        observation.expected_deadline_ticks + deadline_offset;
    if (batch.last_sequence != expected_last_sequence ||
        batch.last_deadline_ticks != expected_last_deadline ||
        batch.last_deadline_ticks > now_ticks) {
        fail(
            observation,
            "level-triggered periodic backlog has noncontiguous or future identity",
            false);
    }
    if (batch.count >
            std::numeric_limits<std::uint64_t>::max() - delivered_edges_ ||
        sequence_offset >
            std::numeric_limits<std::uint64_t>::max() -
                stats_.coalesced_level_edges ||
        stats_.delivered_interrupts ==
            std::numeric_limits<std::uint64_t>::max() ||
        (sequence_offset != 0u &&
         stats_.level_coalescing_events ==
             std::numeric_limits<std::uint64_t>::max())) {
        fail(
            observation,
            "level-triggered periodic audit counter overflow",
            true);
    }

    account_observation(observation);
    delivered_edges_ += batch.count;
    stats_.delivered_edges = delivered_edges_;
    ++stats_.delivered_interrupts;
    stats_.coalesced_level_edges += sequence_offset;
    if (sequence_offset != 0u) {
        ++stats_.level_coalescing_events;
    }
    stats_.missed_or_coalesced_edges = stats_.coalesced_level_edges;
    stats_.last_delivered_sequence = batch.last_sequence;
    return DeadlineEvent{kind, batch.last_sequence, batch.last_deadline_ticks};
}

DeadlineBroker::DeadlineBroker(
    RuntimeTimeline& timeline,
    PendingEventLatch& latch)
    : timeline_(timeline), latch_(latch) {
    wake_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (wake_event_ == nullptr) {
        const DWORD error = GetLastError();
        throw std::system_error(
            static_cast<int>(error),
            std::system_category(),
            "failed to create deadline broker wake event");
    }
}

DeadlineBroker::~DeadlineBroker() {
    stop();
    if (wake_event_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(wake_event_));
        wake_event_ = nullptr;
    }
}

void DeadlineBroker::notify_worker() noexcept {
    if (wake_event_ != nullptr) {
        static_cast<void>(SetEvent(static_cast<HANDLE>(wake_event_)));
    }
}

void DeadlineBroker::set_periodic(
    EventKind kind,
    std::uint64_t first_deadline_ticks,
    std::uint64_t period_ticks,
    std::uint64_t first_sequence) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("deadline broker is stopping");
        }
        schedule_.set_periodic(
            kind, first_deadline_ticks, period_ticks, first_sequence);
        // Replacing a periodic clock also invalidates any edge published by
        // its old phase. The worker publishes while holding this mutex, so the
        // latch clear linearizes with both the prior and replacement schedule.
        (void)latch_.take_all(kind);
    }
    notify_worker();
}

void DeadlineBroker::cancel_periodic(EventKind kind) noexcept {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        schedule_.cancel_periodic(kind);
    }
    notify_worker();
}

void DeadlineBroker::schedule_once(
    EventKind kind,
    std::uint64_t deadline_ticks,
    std::optional<std::uint64_t> sequence) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("deadline broker is stopping");
        }
        schedule_.schedule_once(kind, deadline_ticks, sequence);
    }
    notify_worker();
}

void DeadlineBroker::replace_once(
    EventKind kind,
    std::uint64_t deadline_ticks,
    std::uint64_t generation) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("deadline broker is stopping");
        }
        schedule_.replace_once(kind, deadline_ticks, generation);
        // The worker publishes replaceable events while holding this same
        // mutex. Clearing here therefore linearizes replacement against both
        // scheduled and already-published older generations.
        latch_.clear_replaceable(kind);
    }
    notify_worker();
}

void DeadlineBroker::restore_replaceable_after_host_suspension(
    EventKind kind,
    std::uint64_t deadline_ticks,
    std::uint64_t generation) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("deadline broker is stopping");
        }
        schedule_.restore_replaceable_after_host_suspension(
            kind, deadline_ticks, generation);
        // A published edge belongs to the discarded host-suspension interval.
        // Clearing it under the same mutex prevents a worker publication from
        // surviving alongside the restored, still-current device generation.
        latch_.clear_replaceable(kind);
    }
    notify_worker();
}

void DeadlineBroker::cancel_once(
    EventKind kind,
    std::uint64_t generation) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("deadline broker is stopping");
        }
        schedule_.cancel_once(kind, generation);
        latch_.clear_replaceable(kind);
    }
    notify_worker();
}

void DeadlineBroker::start() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (started_) {
        return;
    }
    if (stopping_) {
        throw std::runtime_error("deadline broker cannot restart after stop");
    }
    // std::thread construction can fail (resource exhaustion). Publish the
    // started state only after ownership of a live worker is established so a
    // caller can cleanly destroy the broker after the exception.
    worker_ = std::thread(&DeadlineBroker::worker_loop, this);
    started_ = true;
}

void DeadlineBroker::stop() noexcept {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    notify_worker();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void DeadlineBroker::throw_if_failed() {
    if (!failed()) {
        return;
    }
    std::exception_ptr failure;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        failure = failure_;
    }
    if (failure != nullptr) {
        std::rethrow_exception(failure);
    }
    throw std::runtime_error("deadline broker failed without an exception");
}

DeadlinePublicationStats DeadlineBroker::publication_stats(
    EventKind kind) const {
    const std::size_t slot = static_cast<std::size_t>(kind);
    if (slot >= publication_stats_.size()) {
        throw std::invalid_argument("invalid deadline event kind");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    return publication_stats_[slot];
}

bool DeadlineBroker::rebase_timeline_after_host_suspension(
    std::uint64_t last_guest_ticks,
    std::uint64_t observed_host_ticks) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) {
        return false;
    }
    return timeline_.rebase_after_host_suspension(
        last_guest_ticks, observed_host_ticks);
}

bool DeadlineBroker::rebase_and_restore_after_host_suspension(
    std::uint64_t last_guest_ticks,
    std::uint64_t observed_host_ticks,
    const std::vector<PeriodicDeadlineRestore>& periodic_restores,
    const std::vector<ReplaceableDeadlineRestore>& replaceable_restores) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return false;
        }

        std::array<bool, kEventKindCount> restored{};
        for (const PeriodicDeadlineRestore& restore : periodic_restores) {
            const std::size_t slot = static_cast<std::size_t>(restore.kind);
            if (slot >= kEventKindCount || restored[slot] ||
                restore.period_ticks == 0u || restore.first_sequence == 0u) {
                throw std::invalid_argument(
                    "invalid periodic host-suspension deadline restore");
            }
            restored[slot] = true;
        }
        for (const ReplaceableDeadlineRestore& restore : replaceable_restores) {
            const std::size_t slot = static_cast<std::size_t>(restore.kind);
            if (slot >= kEventKindCount || restored[slot] ||
                restore.generation == 0u) {
                throw std::invalid_argument(
                    "invalid replaceable host-suspension deadline restore");
            }
            restored[slot] = true;
        }

        // An input/AI/guest service may have exposed the candidate's raw tick
        // while recovery was deferred. Decline before changing any schedule
        // or latch: those observations now belong to the live timeline.
        if (!timeline_.can_rebase_after_host_suspension(
                last_guest_ticks, observed_host_ticks)) {
            return false;
        }

        // Validate owners and acquire memory on an isolated schedule. A late
        // invalid generation must not partially restore schedules or clear
        // publications belonging to the still-live timeline.
        DeadlineSchedule restored_schedule = schedule_;
        for (const PeriodicDeadlineRestore& restore : periodic_restores) {
            restored_schedule.set_periodic(
                restore.kind,
                restore.first_deadline_ticks,
                restore.period_ticks,
                restore.first_sequence);
        }
        for (const ReplaceableDeadlineRestore& restore : replaceable_restores) {
            restored_schedule.restore_replaceable_after_host_suspension(
                restore.kind, restore.deadline_ticks, restore.generation);
        }
        if (!timeline_.rebase_after_host_suspension(
                last_guest_ticks, observed_host_ticks)) {
            return false;
        }
        schedule_ = std::move(restored_schedule);
        for (const PeriodicDeadlineRestore& restore : periodic_restores) {
            (void)latch_.take_all(restore.kind);
        }
        for (const ReplaceableDeadlineRestore& restore : replaceable_restores) {
            latch_.clear_replaceable(restore.kind);
        }
    }
    // The caller runs on the simulation thread. The worker sees the new
    // timeline only after the preceding mutex has been released, with all
    // restored schedules and cleared stale publications already linearized.
    notify_worker();
    return true;
}

void DeadlineBroker::worker_loop() noexcept {
    try {
        // Do this on the worker itself before it reads the timeline or
        // publishes any deadline.  Any scheduling failure is caught below and
        // follows the same RuntimeFailure path as all other broker failures.
        configure_deadline_worker_scheduling();
        const ScopedMmcssDeadlineWorker mmcss_worker;
        const ScopedDeadlineWaitTimer deadline_wait_timer;
        const HANDLE wake_event = static_cast<HANDLE>(wake_event_);
        for (;;) {
            std::optional<std::chrono::nanoseconds> wait_duration;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (stopping_) {
                    return;
                }
                const std::uint64_t now_ticks = timeline_.now_ticks();
                if (timeline_.saturated()) {
                    throw std::overflow_error("runtime timeline saturated");
                }
                const std::vector<DeadlineEvent> due =
                    schedule_.collect_due(now_ticks);
                if (due.empty()) {
                    const std::optional<std::uint64_t> next =
                        schedule_.next_deadline_ticks();
                    if (next.has_value() && *next > now_ticks) {
                        wait_duration =
                            timeline_wait_duration(*next - now_ticks);
                    }
                } else {
                    // Publication stays under `mutex_`: replace_once/cancel_once
                    // cannot return while an obsolete event is still in flight.
                    for (const DeadlineEvent& event : due) {
                        const std::uint64_t prepublication_ticks =
                            timeline_.now_ticks();
                        const bool published = event.replaceable
                            ? latch_.publish_replaceable(
                                  event.kind, event.sequence)
                            : (event.kind == EventKind::InputReport ||
                                       event.kind == EventKind::VideoInterface
                                   ? latch_.publish_periodic(event)
                                   : latch_.publish(event.kind));
                        const std::uint64_t postpublication_ticks =
                            timeline_.now_ticks();
                        if (!published) {
                            throw std::overflow_error(
                                event.replaceable
                                    ? "replaceable deadline publication collided"
                                    : "pending deadline event counter overflowed");
                        }

                        DeadlinePublicationStats& stats = publication_stats_[
                            static_cast<std::size_t>(event.kind)];
                        const std::uint64_t prepublication_lateness =
                            prepublication_ticks >= event.deadline_ticks
                                ? prepublication_ticks - event.deadline_ticks
                                : 0u;
                        const std::uint64_t postpublication_lateness =
                            postpublication_ticks >= event.deadline_ticks
                                ? postpublication_ticks - event.deadline_ticks
                                : 0u;
                        ++stats.publications;
                        stats.last_sequence = event.sequence;
                        stats.last_deadline_ticks = event.deadline_ticks;
                        stats.last_prepublication_ticks = prepublication_ticks;
                        stats.last_postpublication_ticks = postpublication_ticks;
                        stats.last_prepublication_lateness_ticks =
                            prepublication_lateness;
                        stats.last_postpublication_lateness_ticks =
                            postpublication_lateness;
                        stats.max_prepublication_lateness_ticks = std::max(
                            stats.max_prepublication_lateness_ticks,
                            prepublication_lateness);
                        stats.max_postpublication_lateness_ticks = std::max(
                            stats.max_postpublication_lateness_ticks,
                            postpublication_lateness);
                    }
                    continue;
                }
            }
            if (wait_duration.has_value()) {
                deadline_wait_timer.wait_for(wake_event, *wait_duration);
            } else {
                deadline_wait_timer.wait_for_signal(wake_event);
            }
        }
    } catch (...) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            failure_ = std::current_exception();
        }
        failed_.store(true, std::memory_order_release);
        (void)latch_.publish(EventKind::RuntimeFailure);
    }
}

}  // namespace galaxy::timing
