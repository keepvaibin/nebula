#include "galaxy/gx/frame_completion.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

namespace galaxy::gx {

FramePeCooperativeWaitStep frame_pe_cooperative_wait_step(
    std::uint64_t elapsed_ms,
    std::uint32_t timeout_ms,
    std::uint32_t slice_ms) {
    if (timeout_ms == 0u || slice_ms == 0u) {
        throw std::invalid_argument(
            "GX cooperative PE wait requires nonzero timeout and slice");
    }
    if (elapsed_ms >= timeout_ms) {
        return FramePeCooperativeWaitStep{0u, true};
    }
    const std::uint64_t remaining = timeout_ms - elapsed_ms;
    return FramePeCooperativeWaitStep{
        static_cast<std::uint32_t>(
            std::min<std::uint64_t>(remaining, slice_ms)),
        false};
}

FramePeCompletionToken FramePeCompletionContract::issue() {
    if (epoch_ == 0u) {
        throw std::logic_error(
            "GX frame PE completion issued before contract initialization");
    }
    if (issued_ == ~std::uint64_t{0}) {
        throw std::overflow_error("GX frame PE completion token overflow");
    }
    const FramePeCompletionToken token{
        epoch_, issued_ + 1u, cadence::FrameTokenKind::EventBearing};
    entries_.push_back(Entry{token});
    issued_ = token.value;
    return token;
}

FramePeCompletionToken FramePeCompletionContract::issue_event_free() {
    if (epoch_ == 0u) {
        throw std::logic_error(
            "GX event-free frame receipt issued before contract initialization");
    }
    if (issued_ == ~std::uint64_t{0}) {
        throw std::overflow_error("GX frame PE completion token overflow");
    }
    const FramePeCompletionToken token{
        epoch_, issued_ + 1u, cadence::FrameTokenKind::EventFree};
    entries_.push_back(Entry{token, Resolution::Submission, 0u});
    issued_ = token.value;
    ++resolved_;
    (void)advance_completed();
    return token;
}

FramePeCompletionContract::Entry&
FramePeCompletionContract::next_resolution_entry(
    FramePeCompletionToken token) {
    if (!token || token.epoch != epoch_ || token.value > issued_) {
        throw std::logic_error(
            "GX frame PE completion resolved an invalid, stale-epoch, or "
            "future token");
    }
    if (token.value <= completed_) {
        throw std::logic_error(
            "GX frame PE completion resolved a duplicate or stale token");
    }
    const std::uint64_t entry_index = token.value - completed_ - 1u;
    if (entry_index >= entries_.size() ||
        entries_[static_cast<std::size_t>(entry_index)].token != token) {
        throw std::logic_error(
            "GX frame PE completion token conservation failed");
    }
    Entry& entry = entries_[static_cast<std::size_t>(entry_index)];
    if (entry.resolution != Resolution::Unresolved) {
        throw std::logic_error(
            "GX frame PE completion token was resolved more than once");
    }
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(entry_index);
         ++index) {
        if (entries_[index].resolution == Resolution::Unresolved) {
            throw std::logic_error(
                "GX frame PE completion resolved tokens out of order");
        }
    }
    return entry;
}

void FramePeCompletionContract::resolve_on_submission(
    FramePeCompletionToken token) {
    Entry& entry = next_resolution_entry(token);
    entry.resolution = Resolution::Submission;
    ++resolved_;
    (void)advance_completed();
}

void FramePeCompletionContract::resolve_to_fence(
    FramePeCompletionToken token,
    std::uint64_t fence_value) {
    if (fence_value == 0u || fence_value <= last_bound_fence_) {
        throw std::logic_error(
            "GX frame PE completion bound a duplicate or stale GPU fence");
    }
    Entry& entry = next_resolution_entry(token);
    entry.resolution = Resolution::Fence;
    entry.fence_value = fence_value;
    last_bound_fence_ = fence_value;
    ++resolved_;
    (void)advance_completed();
}

bool FramePeCompletionContract::observe_completed_fence(
    std::uint64_t completed_fence_value) {
    if (completed_fence_value < observed_completed_fence_) {
        throw std::logic_error(
            "GX frame PE completion observed a regressing GPU fence");
    }
    observed_completed_fence_ = completed_fence_value;
    return advance_completed();
}

bool FramePeCompletionContract::advance_completed() {
    const std::uint64_t before = completed_;
    while (!entries_.empty()) {
        const Entry& entry = entries_.front();
        if (entry.token.value != completed_ + 1u) {
            throw std::logic_error(
                "GX frame PE completion queue lost token order");
        }
        const bool ready = entry.resolution == Resolution::Submission ||
            (entry.resolution == Resolution::Fence &&
             entry.fence_value <= observed_completed_fence_);
        if (!ready) {
            break;
        }
        entries_.pop_front();
        ++completed_;
    }
    return completed_ != before;
}

void FramePeCompletionContract::validate_wait_token(
    FramePeCompletionToken token) const {
    if (!token || token.epoch != epoch_ || token.value > issued_) {
        throw std::logic_error(
            "GX frame PE completion wait used an invalid, stale-epoch, or "
            "future token");
    }
    const std::uint64_t expected = consumed_ + 1u;
    if (token.value < expected) {
        throw std::logic_error(
            "GX frame PE completion token was waited more than once or is stale");
    }
    if (token.value > expected) {
        throw std::logic_error(
            "GX frame PE completion waits are out of order");
    }
}

FramePeWaitState FramePeCompletionContract::probe_wait(
    FramePeCompletionToken token) const {
    validate_wait_token(token);
    return token.value <= completed_
        ? FramePeWaitState::Complete
        : FramePeWaitState::Pending;
}

void FramePeCompletionContract::consume_wait(
    FramePeCompletionToken token) {
    if (probe_wait(token) != FramePeWaitState::Complete) {
        throw std::logic_error(
            "GX frame PE completion consumed a pending token");
    }
    ++consumed_;
}

FramePeCompletionStats FramePeCompletionContract::stats() const noexcept {
    FramePeCompletionStats result{};
    result.epoch = epoch_;
    result.issued = issued_;
    result.resolved = resolved_;
    result.completed = completed_;
    result.consumed = consumed_;
    for (const Entry& entry : entries_) {
        switch (entry.resolution) {
        case Resolution::Unresolved:
            ++result.unresolved;
            break;
        case Resolution::Submission:
            ++result.submission_ready;
            break;
        case Resolution::Fence:
            ++result.fence_pending;
            break;
        }
    }
    return result;
}

void FramePeCompletionContract::audit_drained() const {
    const FramePeCompletionStats snapshot = stats();
    const bool conserved =
        snapshot.issued == snapshot.completed + snapshot.unresolved +
                snapshot.submission_ready + snapshot.fence_pending &&
        snapshot.consumed <= snapshot.completed &&
        snapshot.completed <= snapshot.resolved &&
        snapshot.resolved <= snapshot.issued;
    if (!conserved) {
        throw std::logic_error(
            "GX frame PE completion conservation failed: epoch=" +
            std::to_string(snapshot.epoch) +
            " issued=" + std::to_string(snapshot.issued) +
            " resolved=" + std::to_string(snapshot.resolved) +
            " completed=" + std::to_string(snapshot.completed) +
            " consumed=" + std::to_string(snapshot.consumed) +
            " unresolved=" + std::to_string(snapshot.unresolved) +
            " submission-ready=" +
            std::to_string(snapshot.submission_ready) +
            " fence-pending=" + std::to_string(snapshot.fence_pending));
    }
    if (snapshot.issued != snapshot.resolved ||
        snapshot.resolved != snapshot.completed ||
        snapshot.completed != snapshot.consumed || !entries_.empty()) {
        throw std::logic_error(
            "GX frame PE completion contract was not drained: epoch=" +
            std::to_string(snapshot.epoch) +
            " issued=" + std::to_string(snapshot.issued) +
            " resolved=" + std::to_string(snapshot.resolved) +
            " completed=" + std::to_string(snapshot.completed) +
            " consumed=" + std::to_string(snapshot.consumed) +
            " pending=" +
            std::to_string(
                snapshot.unresolved + snapshot.submission_ready +
                snapshot.fence_pending));
    }
}

void FramePeCompletionContract::reset() {
    audit_drained();
    if (epoch_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("GX frame PE completion epoch overflow");
    }
    entries_.clear();
    ++epoch_;
    issued_ = 0;
    resolved_ = 0;
    completed_ = 0;
    consumed_ = 0;
    last_bound_fence_ = 0;
    observed_completed_fence_ = 0;
}

}  // namespace galaxy::gx
