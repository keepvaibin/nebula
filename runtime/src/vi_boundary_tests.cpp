#include "galaxy/vi_boundary.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "VI boundary contract failed: " << message << '\n';
        std::exit(1);
    }
}

galaxy::vi::BoundaryCapture capture_with_fifo(std::uint64_t serial = 7u) {
    galaxy::vi::BoundaryCapture capture{};
    capture.serial = serial;
    capture.edge = galaxy::timing::DeadlineEvent{
        galaxy::timing::EventKind::VideoInterface,
        93u,
        10'000u,
        false};
    capture.interrupted_resume_pc = 0x80300010u;
    capture.interrupted_context = 0x80650878u;
    capture.fifo = {
        std::byte{0x61},
        std::byte{0x45},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x02}};
    capture.use_realtime_vi = true;
    capture.present_this_vi = true;
    capture.present_in_render_frame = false;
    capture.xfb_copies_before_render = 41u;
    capture.started = std::chrono::steady_clock::now();
    return capture;
}

void test_transfer_safe_token_and_vi_linearity() {
    using namespace galaxy::vi;
    PendingBoundary boundary;
    require(
        boundary.capture(capture_with_fifo()),
        "one exact deadline and FIFO capture begins");
    require(
        boundary.blocks_context(0x80650878u),
        "the interrupted owner cannot pass the unfinished GPU boundary");
    require(
        !boundary.blocks_context(0x807ACCA0u),
        "the scheduler may run a different exact guest thread");

    const galaxy::gx::FramePeCompletionToken token{3u, 19u};
    require(
        boundary.mark_submitted(token, true),
        "one non-empty FIFO submission stores one token");
    require(
        boundary.captured_fifo().empty(),
        "submitted immutable bytes are released after the backend copies them");
    require(
        boundary.mark_token_consumed(token),
        "the exact completed token is persisted as consumed");
    require(
        boundary.promote_present_this_vi(),
        "presentation may be promoted after exact token consumption");
    require(
        boundary.snapshot().present_this_vi,
        "the presentation promotion survives later guest transfers");
    require(
        !boundary.mark_token_consumed(token),
        "a transferred continuation cannot consume the token twice");
    require(
        !boundary.blocks_context(0x80650878u),
        "the owner is released after the exact GPU token is consumed");
    require(boundary.begin_pe_drain(), "PE drain follows token consumption");
    require(
        boundary.mark_vi_level_latched(),
        "the VI device level is latched exactly once after PE drain");
    require(boundary.await_vi_rfi(), "the boundary waits for exact VI RFI");
    require(
        !boundary.confirm_vi_rfi(8u, 24u),
        "a different boundary serial cannot finalize this edge");
    require(
        !boundary.confirm_vi_rfi(7u, 5u),
        "an AI-selected RFI cannot masquerade as VI completion");
    require(
        boundary.active() &&
            boundary.snapshot().phase == BoundaryPhase::AwaitViRfi &&
            checkpoint_video_service_due(
                boundary.active(),
                /*cadence_unarmed=*/false,
                /*deadline_published=*/false) &&
            select_checkpoint_lead_service(true, true) ==
                CheckpointLeadService::Video,
        "an AI transfer retains and immediately re-leads the consumed VI edge");
    require(
        boundary.confirm_vi_rfi(7u, 24u),
        "only IRQ24 RFI owned by this serial finalizes the boundary");
    require(
        boundary.set_xfb_copies_after_present(42u),
        "final presentation accounting is stored before release");

    BoundarySnapshot completed{};
    require(boundary.finish(completed), "one finalized VI edge is released");
    require(
        completed.edge.sequence == 93u &&
            completed.rendered_fifo_size == 5u &&
            completed.token == token && completed.token_consumed &&
            completed.vi_rfi_seen &&
            completed.xfb_copies_before_render == 41u &&
            completed.xfb_copies_after_present == 42u,
        "finalization preserves every frozen identity and counter");
    require(!boundary.active(), "finalization returns to the empty state");
    require(
        !boundary.finish(completed),
        "the same consumed VI edge cannot finalize twice");
}

void test_would_block_preserves_immutable_capture() {
    using namespace galaxy::vi;
    PendingBoundary boundary;
    require(boundary.capture(capture_with_fifo(11u)), "capture begins");
    const std::array expected{
        std::byte{0x61},
        std::byte{0x45},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x02}};
    require(boundary.mark_submit_pending(), "submit becomes pending");
    require(
        boundary.mark_submit_would_block(),
        "a saturated native queue reports a retryable boundary");
    require(
        boundary.captured_fifo().size() == expected.size(),
        "WouldBlock retains every captured FIFO byte");
    for (std::size_t i = 0; i < expected.size(); ++i) {
        require(
            boundary.captured_fifo()[i] == expected[i],
            "WouldBlock cannot mutate captured FIFO identity");
    }
    require(
        boundary.snapshot().phase == BoundaryPhase::SubmitPending &&
            boundary.snapshot().serial == 11u,
        "WouldBlock preserves phase and serial for exact retry");
}

void test_empty_capture_has_no_fabricated_token() {
    using namespace galaxy::vi;
    PendingBoundary boundary;
    auto capture = capture_with_fifo(13u);
    capture.fifo.clear();
    require(boundary.capture(std::move(capture)), "empty retrace capture begins");
    require(
        boundary.mark_submitted({}, true),
        "an empty capture advances without a fabricated PE token");
    require(
        boundary.snapshot().phase == BoundaryPhase::TokenConsumed &&
            boundary.snapshot().token_consumed &&
            !boundary.snapshot().token,
        "empty capture records exact no-token completion");
    require(boundary.begin_pe_drain(), "empty capture still observes PE drain");
}

void test_invalid_transitions_do_not_mutate_state() {
    using namespace galaxy::vi;
    PendingBoundary boundary;
    const auto token = galaxy::gx::FramePeCompletionToken{1u, 1u};
    require(
        !boundary.mark_submitted(token, true) &&
            !boundary.mark_token_consumed(token) &&
            !boundary.begin_pe_drain() &&
            !boundary.mark_vi_level_latched() &&
            !boundary.await_vi_rfi() &&
            !boundary.confirm_vi_rfi(1u, 24u),
        "transitions cannot start without an exact captured edge");
    require(!boundary.active(), "rejected transitions retain empty state");

    auto invalid = capture_with_fifo();
    invalid.edge.kind = galaxy::timing::EventKind::AudioDma;
    require(
        !boundary.capture(std::move(invalid)),
        "a non-VI deadline cannot enter the VI state machine");
    require(!boundary.active(), "rejected capture cannot partially initialize");
}

void test_checkpoint_lead_service_preserves_vi_transaction_ownership() {
    using namespace galaxy::vi;
    require(
        !checkpoint_video_service_due(false, false, false),
        "an armed cadence with no edge and no boundary needs no VI service");
    require(
        checkpoint_video_service_due(false, true, false),
        "an unarmed cadence still needs the VI readiness probe");
    require(
        checkpoint_video_service_due(false, false, true),
        "a published VI deadline needs service");
    require(
        checkpoint_video_service_due(true, false, false),
        "a consumed VI edge remains serviceable without a second deadline");

    require(
        select_checkpoint_lead_service(false, false) ==
            CheckpointLeadService::None,
        "an idle checkpoint has no leading service");
    require(
        select_checkpoint_lead_service(false, true) ==
            CheckpointLeadService::Audio,
        "audio leads when no VI transaction needs service");
    require(
        select_checkpoint_lead_service(true, false) ==
            CheckpointLeadService::Video,
        "a due VI transaction leads without audio");
    require(
        select_checkpoint_lead_service(true, true) ==
            CheckpointLeadService::Video,
        "a VI transaction is made durable before an audio phase can transfer");
}

void test_active_boundary_ready_successor_host_suspension_evidence() {
    using namespace galaxy::vi;
    using galaxy::timing::PeriodicConsumeStatus;

    PendingBoundary boundary;
    auto capture = capture_with_fifo();
    capture.fifo.clear();
    require(
        boundary.capture(std::move(capture)),
        "evidence capture begins");
    require(
        boundary.mark_submitted({}, true),
        "evidence capture reaches token-consumed state");
    require(boundary.begin_pe_drain(), "evidence capture begins PE drain");
    require(
        boundary.mark_vi_level_latched(),
        "evidence capture latches its VI level");
    require(boundary.await_vi_rfi(), "evidence capture awaits IRQ24 RFI");

    const auto snapshot = boundary.snapshot();
    galaxy::timing::PeriodicDeadlineObservation ready{};
    ready.status = PeriodicConsumeStatus::Ready;
    ready.expected_sequence = snapshot.edge.sequence + 1u;
    ready.expected_deadline_ticks =
        snapshot.edge.deadline_ticks + galaxy::timing::kViPeriodTicks;
    ready.pending_edges = 1u;
    ready.scheduled_due_edges = 1u;
    ready.scheduled_edges_elapsed = ready.expected_sequence;
    ready.lateness_ticks = galaxy::timing::kViPeriodTicks - 1u;
    ready.missed_or_coalesced_edges = 0u;
    const std::uint64_t last_guest_ticks =
        snapshot.edge.deadline_ticks + 1u;

    require(
        active_boundary_owns_ready_vi_successor(
            snapshot, ready, last_guest_ticks),
        "AwaitViRfi ownership proves exactly one ready successor");

    auto token_pending = snapshot;
    token_pending.phase = BoundaryPhase::TokenPending;
    token_pending.token = galaxy::gx::FramePeCompletionToken{3u, 19u};
    token_pending.token_consumed = false;
    require(
        active_boundary_has_exclusive_pre_delivery_token(
            token_pending, false, 0u),
        "one live pre-delivery token owns the active VI boundary");
    require(
        active_boundary_owns_ready_vi_successor(
            token_pending, ready, last_guest_ticks),
        "TokenPending ownership proves exactly one ready successor");
    require(
        !active_boundary_has_exclusive_pre_delivery_token(
            token_pending, true, 0u),
        "an active external dispatcher rejects token-only ownership");
    require(
        !active_boundary_has_exclusive_pre_delivery_token(
            token_pending, false, token_pending.serial),
        "a retained VI dispatcher owner rejects token-only ownership");
    auto invalid_token = token_pending;
    invalid_token.token = {};
    require(
        !active_boundary_owns_ready_vi_successor(
            invalid_token, ready, last_guest_ticks),
        "TokenPending without a live token cannot anchor a successor");
    auto consumed_token = token_pending;
    consumed_token.token_consumed = true;
    require(
        !active_boundary_owns_ready_vi_successor(
            consumed_token, ready, last_guest_ticks),
        "an already consumed token cannot anchor a successor");

    auto rejected = ready;
    rejected.status = PeriodicConsumeStatus::Backlog;
    require(
        !active_boundary_owns_ready_vi_successor(
            snapshot, rejected, last_guest_ticks),
        "backlog evidence cannot use the single-ready-edge exception");
    rejected = ready;
    rejected.pending_edges = 2u;
    require(
        !active_boundary_owns_ready_vi_successor(
            snapshot, rejected, last_guest_ticks),
        "multiple pending edges are never a ready successor");
    rejected = ready;
    rejected.scheduled_due_edges = 2u;
    require(
        !active_boundary_owns_ready_vi_successor(
            snapshot, rejected, last_guest_ticks),
        "multiple elapsed edges are never a ready successor");
    rejected = ready;
    ++rejected.expected_sequence;
    require(
        !active_boundary_owns_ready_vi_successor(
            snapshot, rejected, last_guest_ticks),
        "a non-successor sequence is rejected");
    rejected = ready;
    ++rejected.expected_deadline_ticks;
    require(
        !active_boundary_owns_ready_vi_successor(
            snapshot, rejected, last_guest_ticks),
        "a rephased successor deadline is rejected");
    rejected = ready;
    rejected.missed_or_coalesced_edges = 1u;
    require(
        !active_boundary_owns_ready_vi_successor(
            snapshot, rejected, last_guest_ticks),
        "coalesced edge evidence is rejected");
    require(
        !active_boundary_owns_ready_vi_successor(
            snapshot, ready, snapshot.edge.deadline_ticks - 1u),
        "a pause beginning before the owned edge is rejected");
    require(
        !active_boundary_owns_ready_vi_successor(
            snapshot, ready, ready.expected_deadline_ticks),
        "a pause beginning after the successor deadline is rejected");

    auto wrong_phase = snapshot;
    wrong_phase.phase = BoundaryPhase::ViLevelLatched;
    require(
        !active_boundary_owns_ready_vi_successor(
            wrong_phase, ready, last_guest_ticks),
        "only a durable AwaitViRfi transaction can anchor the successor");
    auto wrong_edge = snapshot;
    wrong_edge.edge.sequence = std::numeric_limits<std::uint64_t>::max();
    require(
        !active_boundary_owns_ready_vi_successor(
            wrong_edge, ready, last_guest_ticks),
        "edge sequence overflow is rejected");
}

void test_active_boundary_successor_during_backlog_evidence() {
    using namespace galaxy::vi;
    using galaxy::timing::PeriodicConsumeStatus;

    PendingBoundary boundary;
    auto capture = capture_with_fifo();
    capture.fifo.clear();
    require(
        boundary.capture(std::move(capture)),
        "backlog evidence capture begins");
    require(
        boundary.mark_submitted({}, true),
        "backlog evidence capture reaches token-consumed state");
    require(boundary.begin_pe_drain(), "backlog evidence capture begins PE drain");
    require(
        boundary.mark_vi_level_latched(),
        "backlog evidence capture latches its VI level");
    require(boundary.await_vi_rfi(), "backlog evidence capture awaits IRQ24 RFI");

    const auto snapshot = boundary.snapshot();
    galaxy::timing::PeriodicDeadlineObservation backlog{};
    backlog.status = PeriodicConsumeStatus::Backlog;
    backlog.expected_sequence = snapshot.edge.sequence + 1u;
    backlog.expected_deadline_ticks =
        snapshot.edge.deadline_ticks + galaxy::timing::kViPeriodTicks;
    // A large backlog (several missed/coalesced periods of WALL time) is
    // exactly the case active_boundary_owns_ready_vi_successor cannot cover
    // -- that is the gap this predicate exists to close.
    backlog.pending_edges = 6u;
    backlog.scheduled_due_edges = 6u;
    backlog.scheduled_edges_elapsed = backlog.expected_sequence;
    backlog.lateness_ticks = galaxy::timing::kViPeriodTicks * 5u;
    backlog.missed_or_coalesced_edges = 5u;
    const std::uint64_t last_guest_ticks = snapshot.edge.deadline_ticks + 1u;

    require(
        active_boundary_owns_successor_during_backlog(
            snapshot, backlog, last_guest_ticks),
        "AwaitViRfi ownership proves the direct successor even under a"
        " large wall-time backlog");

    auto token_pending = snapshot;
    token_pending.phase = BoundaryPhase::TokenPending;
    token_pending.token = galaxy::gx::FramePeCompletionToken{7u, 23u};
    token_pending.token_consumed = false;
    require(
        active_boundary_has_exclusive_pre_delivery_token(
            token_pending, false, 0u),
        "one live pre-delivery token exclusively owns a backlog boundary");
    require(
        active_boundary_owns_successor_during_backlog(
            token_pending, backlog, last_guest_ticks),
        "TokenPending ownership proves the direct successor under a"
        " wall-time backlog");
    auto missing_token = token_pending;
    missing_token.token = {};
    require(
        !active_boundary_owns_successor_during_backlog(
            missing_token, backlog, last_guest_ticks),
        "TokenPending without a live token cannot anchor a backlog successor");
    auto consumed_token = token_pending;
    consumed_token.token_consumed = true;
    require(
        !active_boundary_owns_successor_during_backlog(
            consumed_token, backlog, last_guest_ticks),
        "a consumed token cannot anchor a backlog successor");

    // The Ready-only predicate must NOT accept this Backlog observation --
    // confirms the two predicates are not accidentally equivalent.
    require(
        !active_boundary_owns_ready_vi_successor(
            snapshot, backlog, last_guest_ticks),
        "the Ready-only predicate still rejects a Backlog observation");

    auto rejected = backlog;
    rejected.status = PeriodicConsumeStatus::Ready;
    require(
        !active_boundary_owns_successor_during_backlog(
            snapshot, rejected, last_guest_ticks),
        "a Ready observation is not this predicate's evidence to accept");
    rejected = backlog;
    rejected.status = PeriodicConsumeStatus::EarlyPublication;
    require(
        !active_boundary_owns_successor_during_backlog(
            snapshot, rejected, last_guest_ticks),
        "early-publication evidence is rejected");
    rejected = backlog;
    ++rejected.expected_sequence;
    require(
        !active_boundary_owns_successor_during_backlog(
            snapshot, rejected, last_guest_ticks),
        "a non-successor sequence is rejected regardless of backlog size");
    rejected = backlog;
    ++rejected.expected_deadline_ticks;
    require(
        !active_boundary_owns_successor_during_backlog(
            snapshot, rejected, last_guest_ticks),
        "a rephased successor deadline is rejected");
    rejected = backlog;
    rejected.expected_sequence += 2u;
    rejected.expected_deadline_ticks += galaxy::timing::kViPeriodTicks;
    require(
        !active_boundary_owns_successor_during_backlog(
            snapshot, rejected, last_guest_ticks),
        "a successor two edges ahead is rejected -- this predicate never"
        " authorizes skipping/coalescing more than the one direct successor");
    require(
        !active_boundary_owns_successor_during_backlog(
            snapshot, backlog, snapshot.edge.deadline_ticks - 1u),
        "a pause beginning before the owned edge is rejected");
    require(
        !active_boundary_owns_successor_during_backlog(
            snapshot, backlog, backlog.expected_deadline_ticks),
        "a pause beginning at/after the successor deadline is rejected");

    auto wrong_phase = snapshot;
    wrong_phase.phase = BoundaryPhase::ViLevelLatched;
    require(
        !active_boundary_owns_successor_during_backlog(
            wrong_phase, backlog, last_guest_ticks),
        "only a durable token or AwaitViRfi transaction can anchor the successor");
    auto already_seen = snapshot;
    already_seen.vi_rfi_seen = true;
    require(
        !active_boundary_owns_successor_during_backlog(
            already_seen, backlog, last_guest_ticks),
        "an already-observed RFI cannot re-anchor a new successor");
    auto wrong_edge = snapshot;
    wrong_edge.edge.sequence = std::numeric_limits<std::uint64_t>::max();
    require(
        !active_boundary_owns_successor_during_backlog(
            wrong_edge, backlog, last_guest_ticks),
        "edge sequence overflow is rejected");
    auto replaceable_edge = snapshot;
    replaceable_edge.edge.replaceable = true;
    require(
        !active_boundary_owns_successor_during_backlog(
            replaceable_edge, backlog, last_guest_ticks),
        "a replaceable edge cannot anchor exact VI successor ownership");
}

void test_independent_vi_retains_detached_gpu_owner() {
    using namespace galaxy::vi;
    PendingBoundary boundary;
    auto capture = capture_with_fifo();
    capture.render_completion_independent = true;
    require(boundary.capture(std::move(capture)), "independent VI capture begins");
    const galaxy::gx::FramePeCompletionToken token{3u, 19u};
    require(boundary.mark_submitted(token, true), "backend token persists first");
    require(!boundary.detach_token_to_receipt({3u, 20u}, 1u) &&
                !boundary.detach_token_to_receipt(token, 0u),
            "wrong token and absent receipt cannot release GPU ownership");
    require(boundary.snapshot().phase == BoundaryPhase::TokenPending &&
                !boundary.snapshot().token_consumed,
            "rejected transfer does not mutate pending token");
    require(boundary.detach_token_to_receipt(token, 11u),
            "exact external receipt owns the still-pending GPU token");
    require(!boundary.blocks_context(0x80650878u) &&
                !boundary.snapshot().token_consumed &&
                boundary.snapshot().detached_receipt_serial == 11u,
            "read-released CPU can advance without forged GPU completion");
    require(!boundary.mark_token_consumed(token) &&
                !boundary.detach_token_to_receipt(token, 12u),
            "IRQ24 owner cannot consume or transfer detached receipt again");
    require(boundary.mark_vi_level_latched() && boundary.await_vi_rfi(),
            "VI level advances independently of GPU readiness");
    require(!boundary.confirm_vi_rfi(7u, 18u) &&
                boundary.confirm_vi_rfi(7u, 24u),
            "only the exact IRQ24 RFI finalizes the VI owner");
    BoundarySnapshot completed{};
    require(boundary.finish(completed) && completed.token == token &&
                !completed.token_consumed &&
                completed.detached_receipt_serial == 11u,
            "VI finalization preserves detached GPU receipt identity");
    PendingBoundary legacy;
    require(legacy.capture(capture_with_fifo()) &&
                legacy.mark_submitted(token, true) &&
                !legacy.detach_token_to_receipt(token, 11u),
            "legacy capture cannot opt into independent completion later");
}

}  // namespace

int main() {
    test_transfer_safe_token_and_vi_linearity();
    test_independent_vi_retains_detached_gpu_owner();
    test_would_block_preserves_immutable_capture();
    test_empty_capture_has_no_fabricated_token();
    test_invalid_transitions_do_not_mutate_state();
    test_checkpoint_lead_service_preserves_vi_transaction_ownership();
    test_active_boundary_ready_successor_host_suspension_evidence();
    test_active_boundary_successor_during_backlog_evidence();
    std::cout << "persistent VI boundary contracts passed\n";
    return 0;
}
