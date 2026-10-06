#pragma once

#include "galaxy/gx/frame_completion.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <stdexcept>
#include <vector>

namespace galaxy::gx {

[[nodiscard]] constexpr bool draw_done_submission_policy_valid(
    bool pe_events_gpu_fence,
    bool async_render_thread,
    bool render_live_memory_wait,
    bool render_memory_snapshot,
    bool sim_thread_pe_scan,
    bool direct_wgpipe_pe_events) noexcept {
    const bool render_memory_owned =
        render_live_memory_wait || render_memory_snapshot;
    return pe_events_gpu_fence && async_render_thread && render_memory_owned &&
           !sim_thread_pe_scan && !direct_wgpipe_pe_events;
}

// Independent VI requires protected memory ownership and actual PE fences.
// Snapshot mode selects this policy before synchronous submission. Its backend
// must seal and detach all live capabilities before submission can return and
// token ownership can admit independent VI; failed capture cannot advance it.
[[nodiscard]] constexpr bool independent_vi_pe_policy_valid(
    bool pe_events_gpu_fence, bool async_render_thread,
    bool render_live_memory_wait, bool sim_thread_pe_scan,
    bool direct_wgpipe_pe_events,
    bool detached_memory_snapshot = false) noexcept {
    return pe_events_gpu_fence && async_render_thread &&
        (render_live_memory_wait || detached_memory_snapshot) && !sim_thread_pe_scan &&
        !direct_wgpipe_pe_events;
}

// RMGE01's triple-buffer MainLoop publishes the prior drawing slot as drawn
// only after GXDrawDone returned. Its real PE finish handler writes exactly 1.
// Distinct slot numbers alone are insufficient: reject cached/uncached aliases
// that still resolve to the same live RAM. No completion state is synthesized.
[[nodiscard]] constexpr bool independent_vi_completed_drawn_valid(
    std::uint8_t gx_finished, std::int16_t drawn, std::int16_t drawing,
    const void* drawn_memory, const void* drawing_memory) noexcept {
    return gx_finished == 1u && drawn >= 0 && drawn < 3 &&
        drawing >= 0 && drawing < 3 && drawn != drawing &&
        drawn_memory != nullptr && drawing_memory != nullptr &&
        drawn_memory != drawing_memory;
}

enum class FifoSubmissionPhase : std::uint8_t {
    Captured,
    Submitted,
    Consumed,
};

struct FifoSubmissionEntry {
    std::uint64_t serial = 0;
    FifoSubmissionPhase phase = FifoSubmissionPhase::Captured;
    FramePeCompletionToken token{};
    std::size_t fifo_size = 0;
    // Nonzero only when this receipt owns a VI capture independently of IRQ24.
    std::uint64_t source_vi_serial = 0;
    std::chrono::steady_clock::time_point started{};
    std::vector<std::byte> fifo{};
};

// Simulation-thread-owned FIFO captures, independent of VI/presentation state.
// The owner must outlive translated guest transfers. No method invokes backend
// work, device service, guest code, or PE callbacks.
//
// Submission contract: the backend must copy the captured bytes and finish its
// source-memory read barrier/snapshot before returning a valid token. Record
// that token immediately, before any service that can unwind guest execution.
// An exception after a backend may have enqueued work is fatal, not permission
// to retry Captured bytes: this class cannot infer an unreturned backend token.
//
// Completion contract: poll/consume the exact front token through the backend,
// then mark_consumed immediately before any later service. Backend token order
// includes VI captures: callers must arbitrate the global issuance order, not
// consume a later VI token ahead of an older off-VI token (or vice versa).
class PendingFifoSubmissions {
public:
    PendingFifoSubmissions() = default;
    PendingFifoSubmissions(const PendingFifoSubmissions&) = delete;
    PendingFifoSubmissions& operator=(const PendingFifoSubmissions&) = delete;

    // The allocation happens before moving bytes, so failure leaves the input
    // vector intact. Empty captures do not require a token and are rejected.
    [[nodiscard]] std::uint64_t capture(
        std::vector<std::byte>&& fifo,
        std::chrono::steady_clock::time_point started) {
        if (fifo.empty()) {
            throw std::logic_error("off-VI FIFO capture must be non-empty");
        }
        if (last_serial_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::logic_error("off-VI FIFO capture serial exhausted");
        }
        entries_.emplace_back();
        auto& entry = entries_.back();
        entry.serial = ++last_serial_;
        entry.fifo_size = fifo.size();
        entry.started = started;
        entry.fifo.swap(fifo);
        return entry.serial;
    }

    // Adopt only a successfully submitted, CPU-read-released VI capture.
    // Allocation failure is fatal: the backend may already own queued work.
    // No guest/device service may occur between backend return and adoption.
    [[nodiscard]] std::uint64_t adopt_submitted_vi(
        FramePeCompletionToken token, std::size_t fifo_size,
        std::chrono::steady_clock::time_point started,
        std::uint64_t source_vi_serial) {
        if (!token || fifo_size == 0u || source_vi_serial == 0u ||
            next_to_submit() != nullptr ||
            (last_submitted_token_ &&
             (token.epoch != last_submitted_token_.epoch ||
              token.value <= last_submitted_token_.value))) {
            throw std::logic_error("invalid detached VI FIFO receipt adoption");
        }
        if (last_serial_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::logic_error("detached VI FIFO receipt serial exhausted");
        }
        entries_.emplace_back();
        auto& entry = entries_.back();
        entry.serial = ++last_serial_;
        entry.phase = FifoSubmissionPhase::Submitted;
        entry.token = token;
        entry.fifo_size = fifo_size;
        entry.source_vi_serial = source_vi_serial;
        entry.started = started;
        last_submitted_token_ = token;
        return entry.serial;
    }

    [[nodiscard]] const FifoSubmissionEntry* front() const noexcept {
        return entries_.empty() ? nullptr : &entries_.front();
    }

    [[nodiscard]] const FifoSubmissionEntry* next_to_submit() const noexcept {
        for (const auto& entry : entries_) {
            if (entry.phase == FifoSubmissionPhase::Captured) {
                return &entry;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool mark_submitted(
        std::uint64_t serial, FramePeCompletionToken token) noexcept {
        const auto* next = next_to_submit();
        if (next == nullptr || next->serial != serial || !token ||
            (last_submitted_token_ &&
             (token.epoch != last_submitted_token_.epoch ||
              token.value <= last_submitted_token_.value))) {
            return false;
        }
        for (auto& entry : entries_) {
            if (entry.serial == serial) {
                entry.token = token;
                entry.phase = FifoSubmissionPhase::Submitted;
                // Backend now owns its independent FIFO copy. Later producer
                // bytes cannot be reached or cleared through this vector.
                entry.fifo.clear();
                last_submitted_token_ = token;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool mark_consumed(
        std::uint64_t serial, FramePeCompletionToken token) noexcept {
        if (entries_.empty()) {
            return false;
        }
        auto& entry = entries_.front();
        if (entry.serial != serial ||
            entry.phase != FifoSubmissionPhase::Submitted ||
            entry.token != token) {
            return false;
        }
        entry.phase = FifoSubmissionPhase::Consumed;
        return true;
    }

    // Keep Consumed distinct so a transfer between backend consumption and
    // retirement resumes without a second wait or submission.
    [[nodiscard]] bool retire_consumed(std::uint64_t serial) noexcept {
        if (entries_.empty() || entries_.front().serial != serial ||
            entries_.front().phase != FifoSubmissionPhase::Consumed) {
            return false;
        }
        entries_.pop_front();
        return true;
    }

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    void audit_drained() const {
        if (!entries_.empty()) {
            throw std::logic_error("off-VI FIFO submission owners remain undrained");
        }
    }

private:
    std::deque<FifoSubmissionEntry> entries_{};
    std::uint64_t last_serial_ = 0;
    FramePeCompletionToken last_submitted_token_{};
};

}  // namespace galaxy::gx
