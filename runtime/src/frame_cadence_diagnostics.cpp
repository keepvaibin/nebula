#include "galaxy/frame_cadence_diagnostics.h"

#include <chrono>

namespace galaxy::cadence {
namespace {

std::atomic<Session*> g_active_session{nullptr};

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

}  // namespace

void TimingAccumulator::record(std::uint64_t elapsed_ns) noexcept {
    count_.fetch_add(1u, std::memory_order_relaxed);
    total_ns_.fetch_add(elapsed_ns, std::memory_order_relaxed);
    update_max(max_ns_, elapsed_ns);
    buckets_[timing_histogram_bucket_for_ns(elapsed_ns)].fetch_add(
        1u, std::memory_order_relaxed);
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

std::uint64_t steady_now_ns_if_enabled(const Session* session) noexcept {
    if (session == nullptr || !session->enabled()) {
        return 0u;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0u;
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

Session* global_session_if_enabled() noexcept {
    return g_active_session.load(std::memory_order_acquire);
}

}  // namespace galaxy::cadence
