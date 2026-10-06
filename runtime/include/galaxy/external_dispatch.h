#pragma once

#include <cstdint>
#include <limits>

namespace galaxy::interrupt {

inline constexpr std::uint32_t kExternalDispatcherHandlerReturnLr =
    0x804A87E8u;
inline constexpr std::uint32_t kNoExternalInterrupt =
    std::numeric_limits<std::uint32_t>::max();

struct ExternalDispatchRecord {
    bool active{};
    std::uint32_t interrupted_context{};
    std::uint32_t resume_pc{};
    std::uint32_t initial_pi_cause{};
    std::uint32_t initial_pi_mask{};
    std::uint32_t selected_interrupt{kNoExternalInterrupt};
    std::uint32_t selected_handler{};
    bool selected_handler_returned{};
};

struct ExternalHandlerCallEvidence {
    std::uint32_t return_lr{};
    std::uint32_t interrupt{};
    std::uint32_t context_argument{};
    std::uint32_t target{};
    std::uint32_t handler_table_target{};
};

// A host-timeline recovery may preserve an in-flight VI transaction only when
// the translated external dispatcher still explains the boundary's wait.
// Before the dispatcher's mask scan reaches its indirect call there is
// deliberately no selected source; after selection, only IRQ24 is compatible
// with an explicitly owned VI boundary. This predicate never selects a source
// or completes an interrupt.
[[nodiscard]] constexpr bool external_dispatch_preserves_vi_boundary_wait(
    const ExternalDispatchRecord& record,
    std::uint64_t owner_serial,
    std::uint64_t boundary_serial,
    std::uint32_t boundary_context,
    std::uint32_t boundary_resume_pc) noexcept {
    if (!record.active || boundary_serial == 0u || boundary_context == 0u ||
        boundary_resume_pc == 0u ||
        record.interrupted_context != boundary_context ||
        record.resume_pc != boundary_resume_pc ||
        (owner_serial != 0u && owner_serial != boundary_serial)) {
        return false;
    }

    const bool scanning_before_selection =
        record.selected_interrupt == kNoExternalInterrupt &&
        record.selected_handler == 0u && !record.selected_handler_returned;
    constexpr std::uint32_t kViInterrupt = 24u;
    const bool selected_vi = record.selected_interrupt == kViInterrupt &&
        record.selected_handler != 0u;
    // owner_serial==0 is the exact PeDrain-entry case: the boundary existed but
    // had not reached AwaitViRfi when this dispatcher began. A nested
    // checkpoint advanced it to AwaitViRfi while the dispatcher was still
    // scanning, so source selection must still be absent. A dispatch that
    // began after AwaitViRfi instead records the boundary serial and may be
    // either scanning or executing/completing IRQ24.
    return owner_serial == 0u ? scanning_before_selection
                              : scanning_before_selection || selected_vi;
}

enum class ExternalHandlerCallDisposition : std::uint8_t {
    NotDispatcherSelection,
    Accepted,
    RejectInactiveDispatch,
    RejectInterruptRange,
    RejectContextArgument,
    RejectNullHandler,
    RejectHandlerTableTarget,
    RejectDuplicateSelection,
};

enum class ExternalHandlerReturnDisposition : std::uint8_t {
    Accepted,
    RejectInactiveDispatch,
    RejectNoSelection,
    RejectHandlerIdentity,
    RejectDuplicateReturn,
};

enum class ExternalRfiDisposition : std::uint8_t {
    AcceptedAfterHandlerReturn,
    RejectInactiveDispatch,
    RejectNoSelection,
    RejectUnfinishedHandler,
};

enum class RfiRouteSource : std::uint8_t {
    Ordinary,
    SynchronousExceptionRetry,
};

enum class ExternalRfiRouteResult : std::uint8_t {
    BypassedForSynchronousException,
    NoExternalDispatch,
    ConsumedExternalDispatch,
    RejectedExternalDispatch,
};

class ExternalDispatchTracker {
public:
    [[nodiscard]] bool begin(
        std::uint32_t interrupted_context,
        std::uint32_t resume_pc,
        std::uint32_t pi_cause,
        std::uint32_t pi_mask) noexcept {
        if (record_.active || interrupted_context == 0u || resume_pc == 0u) {
            return false;
        }
        record_ = ExternalDispatchRecord{
            true,
            interrupted_context,
            resume_pc,
            pi_cause,
            pi_mask,
            kNoExternalInterrupt,
            0u,
            false};
        return true;
    }

    [[nodiscard]] ExternalHandlerCallDisposition enter_handler(
        const ExternalHandlerCallEvidence& evidence) noexcept {
        if (evidence.return_lr != kExternalDispatcherHandlerReturnLr) {
            return ExternalHandlerCallDisposition::NotDispatcherSelection;
        }
        if (!record_.active) {
            return ExternalHandlerCallDisposition::RejectInactiveDispatch;
        }
        if (evidence.interrupt >= 32u) {
            return ExternalHandlerCallDisposition::RejectInterruptRange;
        }
        if (evidence.context_argument != record_.interrupted_context) {
            return ExternalHandlerCallDisposition::RejectContextArgument;
        }
        if (evidence.target == 0u || evidence.handler_table_target == 0u) {
            return ExternalHandlerCallDisposition::RejectNullHandler;
        }
        if (evidence.target != evidence.handler_table_target) {
            return ExternalHandlerCallDisposition::RejectHandlerTableTarget;
        }
        if (record_.selected_interrupt != kNoExternalInterrupt) {
            return ExternalHandlerCallDisposition::RejectDuplicateSelection;
        }
        record_.selected_interrupt = evidence.interrupt;
        record_.selected_handler = evidence.target;
        record_.selected_handler_returned = false;
        return ExternalHandlerCallDisposition::Accepted;
    }

    [[nodiscard]] ExternalHandlerReturnDisposition finish_handler(
        std::uint32_t interrupt,
        std::uint32_t handler) noexcept {
        if (!record_.active) {
            return ExternalHandlerReturnDisposition::RejectInactiveDispatch;
        }
        if (record_.selected_interrupt == kNoExternalInterrupt) {
            return ExternalHandlerReturnDisposition::RejectNoSelection;
        }
        if (record_.selected_interrupt != interrupt ||
            record_.selected_handler != handler) {
            return ExternalHandlerReturnDisposition::RejectHandlerIdentity;
        }
        if (record_.selected_handler_returned) {
            return ExternalHandlerReturnDisposition::RejectDuplicateReturn;
        }
        record_.selected_handler_returned = true;
        return ExternalHandlerReturnDisposition::Accepted;
    }

    [[nodiscard]] ExternalRfiDisposition rfi_disposition() const noexcept {
        if (!record_.active) {
            return ExternalRfiDisposition::RejectInactiveDispatch;
        }
        if (record_.selected_interrupt == kNoExternalInterrupt) {
            return ExternalRfiDisposition::RejectNoSelection;
        }
        return record_.selected_handler_returned
            ? ExternalRfiDisposition::AcceptedAfterHandlerReturn
            : ExternalRfiDisposition::RejectUnfinishedHandler;
    }

    [[nodiscard]] bool take_for_rfi(
        ExternalDispatchRecord& completed) noexcept {
        const ExternalRfiDisposition disposition = rfi_disposition();
        if (disposition != ExternalRfiDisposition::AcceptedAfterHandlerReturn) {
            return false;
        }
        completed = record_;
        record_ = {};
        return true;
    }

    [[nodiscard]] bool active() const noexcept { return record_.active; }
    [[nodiscard]] const ExternalDispatchRecord& record() const noexcept {
        return record_;
    }

private:
    ExternalDispatchRecord record_{};
};

// A synchronous exception (notably lazy-FPU exception 7) can occur while a
// translated external-interrupt handler is still running. Its inner RFI must
// return to the suppressed instruction without inspecting or consuming the
// outer dispatch record. Keep that ownership decision together with the only
// operation that may consume the record, so later interrupt sources cannot
// accidentally collapse two nested architectural returns into one.
[[nodiscard]] inline ExternalRfiRouteResult route_external_rfi(
    ExternalDispatchTracker& tracker,
    RfiRouteSource source,
    ExternalDispatchRecord& completed) noexcept {
    if (source == RfiRouteSource::SynchronousExceptionRetry) {
        return ExternalRfiRouteResult::BypassedForSynchronousException;
    }
    if (!tracker.active()) {
        return ExternalRfiRouteResult::NoExternalDispatch;
    }
    return tracker.take_for_rfi(completed)
        ? ExternalRfiRouteResult::ConsumedExternalDispatch
        : ExternalRfiRouteResult::RejectedExternalDispatch;
}

}  // namespace galaxy::interrupt
