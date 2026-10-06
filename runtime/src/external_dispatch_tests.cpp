#include "galaxy/external_dispatch.h"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "external dispatch contract failed: " << message << '\n';
        std::exit(1);
    }
}

galaxy::interrupt::ExternalHandlerCallEvidence valid_call() {
    return galaxy::interrupt::ExternalHandlerCallEvidence{
        galaxy::interrupt::kExternalDispatcherHandlerReturnLr,
        5u,
        0x80650878u,
        0x804C8140u,
        0x804C8140u};
}

void test_exact_handler_selection_and_rfi() {
    using namespace galaxy::interrupt;
    ExternalDispatchTracker tracker;
    require(
        tracker.begin(
            0x80650878u, 0x80300010u, 0x00000080u, 0x00000080u),
        "an exact external exception begins once");
    require(
        !tracker.begin(
            0x80650878u, 0x80300014u, 0x00000080u, 0x00000080u),
        "a second wrapper cannot overlap the first");
    require(
        tracker.enter_handler(valid_call()) ==
            ExternalHandlerCallDisposition::Accepted,
        "the exact dispatcher indirect call is accepted");
    require(
        tracker.rfi_disposition() ==
            ExternalRfiDisposition::RejectUnfinishedHandler,
        "RFI cannot finalize a handler whose native call has not returned");
    require(
        tracker.finish_handler(5u, 0x804C8140u) ==
            ExternalHandlerReturnDisposition::Accepted,
        "the selected handler returns exactly once");
    require(
        tracker.rfi_disposition() ==
            ExternalRfiDisposition::AcceptedAfterHandlerReturn,
        "RFI is accepted after the selected handler returns");

    ExternalDispatchRecord completed{};
    require(
        tracker.take_for_rfi(completed),
        "the valid RFI consumes the active dispatch");
    require(
        completed.selected_interrupt == 5u &&
            completed.selected_handler == 0x804C8140u &&
            completed.selected_handler_returned,
        "RFI preserves the exact selected-source identity");
    require(!tracker.active(), "RFI leaves no stale external dispatch");
}

void test_no_software_handler_selection_fails_closed() {
    using namespace galaxy::interrupt;
    ExternalDispatchTracker tracker;
    require(
        tracker.begin(
            0x80650C90u, 0x804AB374u, 0x00000100u, 0x00000100u),
        "the exact idle context can own an external exception record");
    require(
        tracker.rfi_disposition() ==
            ExternalRfiDisposition::RejectNoSelection,
        "an entered hardware exception must select a registered guest source");
    ExternalDispatchRecord completed{};
    require(
        !tracker.take_for_rfi(completed),
        "a no-selection dispatcher return cannot be consumed");
    require(
        tracker.active() &&
            tracker.record().selected_interrupt == kNoExternalInterrupt,
        "rejected no-selection RFI preserves evidence for the fatal report");
}

void test_unrelated_calls_do_not_select_a_source() {
    using namespace galaxy::interrupt;
    ExternalDispatchTracker tracker;
    require(
        tracker.begin(
            0x80650878u, 0x80300010u, 0x00000080u, 0x00000080u),
        "test dispatch begins");
    auto evidence = valid_call();
    evidence.return_lr = 0x804A87D4u;
    require(
        tracker.enter_handler(evidence) ==
            ExternalHandlerCallDisposition::NotDispatcherSelection,
        "a different translated call site is observational only");
    require(
        tracker.record().selected_interrupt == kNoExternalInterrupt,
        "an unrelated call cannot claim the source");
}

void test_selection_mismatches_fail_closed_without_mutation() {
    using namespace galaxy::interrupt;
    const auto exercise = [](ExternalHandlerCallEvidence evidence,
                             ExternalHandlerCallDisposition expected,
                             const char* message) {
        ExternalDispatchTracker tracker;
        require(
            tracker.begin(
                0x80650878u, 0x80300010u, 0x00000080u, 0x00000080u),
            "mismatch test dispatch begins");
        require(tracker.enter_handler(evidence) == expected, message);
        require(
            tracker.record().selected_interrupt == kNoExternalInterrupt &&
                tracker.record().selected_handler == 0u,
            "rejected selection cannot mutate source identity");
    };

    auto evidence = valid_call();
    evidence.interrupt = 32u;
    exercise(
        evidence,
        ExternalHandlerCallDisposition::RejectInterruptRange,
        "an out-of-range OS interrupt is rejected");

    evidence = valid_call();
    evidence.context_argument = 0x807ACCA0u;
    exercise(
        evidence,
        ExternalHandlerCallDisposition::RejectContextArgument,
        "the dispatcher must pass the exact interrupted context");

    evidence = valid_call();
    evidence.target = 0u;
    evidence.handler_table_target = 0u;
    exercise(
        evidence,
        ExternalHandlerCallDisposition::RejectNullHandler,
        "a null selected handler is rejected");

    evidence = valid_call();
    evidence.handler_table_target = 0x804C8200u;
    exercise(
        evidence,
        ExternalHandlerCallDisposition::RejectHandlerTableTarget,
        "the indirect target must equal the current handler table slot");
}

void test_duplicate_and_foreign_returns_are_rejected() {
    using namespace galaxy::interrupt;
    ExternalDispatchTracker tracker;
    require(
        tracker.begin(
            0x80650878u, 0x80300010u, 0x00000080u, 0x00000080u),
        "duplicate test dispatch begins");
    require(
        tracker.enter_handler(valid_call()) ==
            ExternalHandlerCallDisposition::Accepted,
        "first source selection is accepted");
    require(
        tracker.enter_handler(valid_call()) ==
            ExternalHandlerCallDisposition::RejectDuplicateSelection,
        "the wrapper cannot select a second source");
    require(
        tracker.finish_handler(7u, 0x804C8140u) ==
            ExternalHandlerReturnDisposition::RejectHandlerIdentity,
        "a foreign interrupt cannot finish the selected handler");
    require(
        tracker.finish_handler(5u, 0x804C8200u) ==
            ExternalHandlerReturnDisposition::RejectHandlerIdentity,
        "a foreign target cannot finish the selected handler");
    require(
        tracker.finish_handler(5u, 0x804C8140u) ==
            ExternalHandlerReturnDisposition::Accepted,
        "the exact handler finishes");
    require(
        tracker.finish_handler(5u, 0x804C8140u) ==
            ExternalHandlerReturnDisposition::RejectDuplicateReturn,
        "the same handler cannot finish twice");
}

void test_combined_latched_sources_remain_guest_selected() {
    using namespace galaxy::interrupt;
    ExternalDispatchTracker tracker;
    constexpr std::uint32_t kCombinedCause = 0x00000140u;
    require(
        tracker.begin(
            0x80650878u,
            0x80300010u,
            kCombinedCause,
            kCombinedCause),
        "one external exception may begin with multiple latched device causes");
    require(
        tracker.record().initial_pi_cause == kCombinedCause &&
            tracker.record().initial_pi_mask == kCombinedCause &&
            tracker.record().selected_interrupt == kNoExternalInterrupt,
        "combined PI state is evidence and cannot host-select a source");

    auto evidence = valid_call();
    evidence.interrupt = 24u;
    require(
        tracker.enter_handler(evidence) ==
            ExternalHandlerCallDisposition::Accepted,
        "the translated dispatcher alone selects from combined causes");
    require(
        tracker.record().selected_interrupt == 24u,
        "the observed guest selection is retained exactly");
}

void test_vi_boundary_owner_requires_exact_dispatch_identity() {
    using namespace galaxy::interrupt;
    constexpr std::uint64_t kSerial = 336u;
    constexpr std::uint32_t kContext = 0x807ACCA0u;
    constexpr std::uint32_t kResume = 0x804A8798u;

    ExternalDispatchTracker tracker;
    require(
        tracker.begin(kContext, kResume, 0x00000100u, 0x00000100u),
        "VI owner test begins an external dispatch");
    require(
        external_dispatch_preserves_vi_boundary_wait(
            tracker.record(), kSerial, kSerial, kContext, kResume),
        "the pre-selection mask scan retains exact VI boundary ownership");
    require(
        !external_dispatch_preserves_vi_boundary_wait(
            tracker.record(), kSerial + 1u, kSerial, kContext, kResume) &&
            !external_dispatch_preserves_vi_boundary_wait(
                tracker.record(),
                kSerial,
                kSerial,
                0x80650878u,
                kResume) &&
            !external_dispatch_preserves_vi_boundary_wait(
                tracker.record(),
                kSerial,
                kSerial,
                kContext,
                kResume + 4u),
        "owned serial, context, and boundary resume identity must match");
    require(
        external_dispatch_preserves_vi_boundary_wait(
            tracker.record(), 0u, kSerial, kContext, kResume),
        "an older pre-selection dispatch may block a boundary captured by its nested checkpoint");

    auto vi_call = valid_call();
    vi_call.interrupt = 24u;
    vi_call.context_argument = kContext;
    require(
        tracker.enter_handler(vi_call) ==
            ExternalHandlerCallDisposition::Accepted,
        "VI owner test selects IRQ24 through the translated dispatcher");
    require(
        external_dispatch_preserves_vi_boundary_wait(
            tracker.record(), kSerial, kSerial, kContext, kResume),
        "the running IRQ24 handler retains VI boundary ownership");
    require(
        tracker.finish_handler(24u, vi_call.target) ==
            ExternalHandlerReturnDisposition::Accepted,
        "VI owner test completes the selected IRQ24 handler");
    require(
        external_dispatch_preserves_vi_boundary_wait(
            tracker.record(), kSerial, kSerial, kContext, kResume),
        "the completed IRQ24 handler retains ownership until its RFI");

    ExternalDispatchTracker foreign_tracker;
    require(
        foreign_tracker.begin(
            kContext, kResume, 0x00000140u, 0x00000140u),
        "foreign-source owner test begins an external dispatch");
    auto foreign_call = valid_call();
    foreign_call.context_argument = kContext;
    require(
        foreign_tracker.enter_handler(foreign_call) ==
            ExternalHandlerCallDisposition::Accepted,
        "translated dispatcher may select a different latched source");
    require(
        !external_dispatch_preserves_vi_boundary_wait(
            foreign_tracker.record(),
            kSerial,
            kSerial,
            kContext,
            kResume),
        "a selected non-VI source cannot claim VI boundary ownership");
    require(
        !external_dispatch_preserves_vi_boundary_wait(
            foreign_tracker.record(), 0u, kSerial, kContext, kResume),
        "an ownerless dispatch is accepted only before source selection");

    ExternalDispatchRecord malformed = tracker.record();
    malformed.selected_interrupt = kNoExternalInterrupt;
    require(
        !external_dispatch_preserves_vi_boundary_wait(
            malformed, kSerial, kSerial, kContext, kResume),
        "a no-selection record with stale handler state is rejected");
    malformed = tracker.record();
    malformed.active = false;
    require(
        !external_dispatch_preserves_vi_boundary_wait(
            malformed, kSerial, kSerial, kContext, kResume),
        "an inactive dispatcher cannot own a VI boundary");
}

bool same_record(
    const galaxy::interrupt::ExternalDispatchRecord& left,
    const galaxy::interrupt::ExternalDispatchRecord& right) {
    return left.active == right.active &&
           left.interrupted_context == right.interrupted_context &&
           left.resume_pc == right.resume_pc &&
           left.initial_pi_cause == right.initial_pi_cause &&
           left.initial_pi_mask == right.initial_pi_mask &&
           left.selected_interrupt == right.selected_interrupt &&
           left.selected_handler == right.selected_handler &&
           left.selected_handler_returned == right.selected_handler_returned;
}

void test_synchronous_exception_rfi_preserves_outer_dispatch() {
    using namespace galaxy::interrupt;
    ExternalDispatchTracker tracker;
    require(
        tracker.begin(
            0x80650878u, 0x80300010u, 0x00000080u, 0x00000080u),
        "nested-RFI test begins an outer external dispatch");
    require(
        tracker.enter_handler(valid_call()) ==
            ExternalHandlerCallDisposition::Accepted,
        "nested-RFI test enters the still-running outer guest handler");
    const ExternalDispatchRecord before_inner_rfi = tracker.record();
    require(
        !before_inner_rfi.selected_handler_returned,
        "inner synchronous exception occurs before the outer handler returns");
    ExternalDispatchRecord completed{
        true,
        0xAAAAAAAAu,
        0xBBBBBBBBu,
        0xCCCCCCCCu,
        0xDDDDDDDDu,
        3u,
        0xEEEEEEEEu,
        true};
    const ExternalDispatchRecord output_sentinel = completed;

    require(
        route_external_rfi(
            tracker,
            RfiRouteSource::SynchronousExceptionRetry,
            completed) ==
            ExternalRfiRouteResult::BypassedForSynchronousException,
        "inner synchronous-exception RFI selects the bypass route");
    require(
        tracker.active() &&
            same_record(tracker.record(), before_inner_rfi) &&
            same_record(completed, output_sentinel),
        "inner synchronous-exception RFI cannot inspect or mutate outer ownership");

    require(
        tracker.finish_handler(5u, 0x804C8140u) ==
            ExternalHandlerReturnDisposition::Accepted,
        "outer guest handler finishes after its suppressed instruction retries");
    const ExternalDispatchRecord before_outer_rfi = tracker.record();

    require(
        route_external_rfi(
            tracker, RfiRouteSource::Ordinary, completed) ==
            ExternalRfiRouteResult::ConsumedExternalDispatch,
        "eventual outer RFI consumes its completed dispatch");
    require(
        !tracker.active() && same_record(completed, before_outer_rfi),
        "outer dispatch is returned exactly once with every evidence field intact");
}

void test_rejected_ordinary_rfi_preserves_incomplete_dispatch() {
    using namespace galaxy::interrupt;
    ExternalDispatchTracker tracker;
    require(
        tracker.begin(
            0x80650878u, 0x80300010u, 0x00000080u, 0x00000080u),
        "incomplete-RFI test begins an outer external dispatch");
    const ExternalDispatchRecord before = tracker.record();
    ExternalDispatchRecord completed{};
    require(
        route_external_rfi(
            tracker, RfiRouteSource::Ordinary, completed) ==
            ExternalRfiRouteResult::RejectedExternalDispatch,
        "ordinary RFI rejects an incomplete external dispatch");
    require(
        tracker.active() && same_record(tracker.record(), before),
        "rejected ordinary RFI leaves all incomplete-dispatch evidence intact");
}

}  // namespace

int main() {
    test_exact_handler_selection_and_rfi();
    test_no_software_handler_selection_fails_closed();
    test_unrelated_calls_do_not_select_a_source();
    test_selection_mismatches_fail_closed_without_mutation();
    test_duplicate_and_foreign_returns_are_rejected();
    test_combined_latched_sources_remain_guest_selected();
    test_vi_boundary_owner_requires_exact_dispatch_identity();
    test_synchronous_exception_rfi_preserves_outer_dispatch();
    test_rejected_ordinary_rfi_preserves_incomplete_dispatch();
    std::cout << "external interrupt dispatch contracts passed\n";
    return 0;
}
