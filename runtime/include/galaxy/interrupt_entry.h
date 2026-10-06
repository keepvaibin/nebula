#pragma once

#include "galaxy/native_api.h"

#include <cstdint>

namespace galaxy::interrupt {

inline constexpr std::uint32_t kMsrPowerManagement = 0x00040000u;
inline constexpr std::uint32_t kMsrInterruptLittleEndian = 0x00010000u;
inline constexpr std::uint32_t kMsrExternalInterruptEnable = 0x00008000u;
inline constexpr std::uint32_t kMsrProblemState = 0x00004000u;
inline constexpr std::uint32_t kMsrFloatingPointAvailable = 0x00002000u;
inline constexpr std::uint32_t kMsrFloatingPointExceptionMode0 = 0x00000800u;
inline constexpr std::uint32_t kMsrSingleStepTrace = 0x00000400u;
inline constexpr std::uint32_t kMsrBranchTrace = 0x00000200u;
inline constexpr std::uint32_t kMsrFloatingPointExceptionMode1 = 0x00000100u;
inline constexpr std::uint32_t kMsrInstructionRelocation = 0x00000020u;
inline constexpr std::uint32_t kMsrDataRelocation = 0x00000010u;
inline constexpr std::uint32_t kMsrPerformanceMonitorMark = 0x00000004u;
inline constexpr std::uint32_t kMsrRecoverableException = 0x00000002u;
inline constexpr std::uint32_t kMsrLittleEndian = 0x00000001u;

// Exact MSR established by the Wii disc bootstrap before the apploader hands
// execution to a retail DOL: supervisor mode, asynchronous interrupts masked,
// floating point and address translation available, and recoverable exception
// state set. RMGE01 immediately ORs FP again in __init_hardware; preserving the
// real handoff still matters because FPU availability is now enforced by every
// translated FP instruction.
inline constexpr std::uint32_t kWiiDiscDolEntryMsr =
    kMsrFloatingPointAvailable | kMsrInstructionRelocation |
    kMsrDataRelocation | kMsrRecoverableException;
static_assert(kWiiDiscDolEntryMsr == 0x00002032u);

// MPC750UM Table 4-6 and Broadway's equivalent exception entry clear these
// live MSR bits for every exception used here. ME, IP, and ILE are retained;
// LE is assigned from the pre-exception ILE bit rather than merely preserved.
inline constexpr std::uint32_t kHardwareExceptionMsrClearBits =
    kMsrPowerManagement | kMsrExternalInterruptEnable | kMsrProblemState |
    kMsrFloatingPointAvailable | kMsrFloatingPointExceptionMode0 |
    kMsrSingleStepTrace | kMsrBranchTrace |
    kMsrFloatingPointExceptionMode1 | kMsrInstructionRelocation |
    kMsrDataRelocation | kMsrPerformanceMonitorMark |
    kMsrRecoverableException;
static_assert(kHardwareExceptionMsrClearBits == 0x0004EF36u);

// Ordinary recoverable exceptions copy only the MPC750/Gekko-defined MSR
// subset into SRR1; reserved/non-equivalent fields are cleared. RMGE01's low
// vector then stores this SRR1 image into OSContext::srr1.
inline constexpr std::uint32_t kHardwareExceptionSrr1Mask = 0x87C0FFFFu;

[[nodiscard]] constexpr std::uint32_t hardware_exception_srr1(
    std::uint32_t saved_msr) noexcept {
    return saved_msr & kHardwareExceptionSrr1Mask;
}

[[nodiscard]] constexpr std::uint32_t hardware_exception_msr(
    std::uint32_t saved_msr) noexcept {
    std::uint32_t live_msr =
        (saved_msr & ~kHardwareExceptionMsrClearBits) & ~kMsrLittleEndian;
    if ((saved_msr & kMsrInterruptLittleEndian) != 0u) {
        live_msr |= kMsrLittleEndian;
    }
    return live_msr;
}

// RMGE01's copied low-memory OSExceptionVector re-enables instruction and
// data relocation in SRR1 and executes RFI before entering the installed C
// handler. The runtime skips only that non-DOL vector, so its direct wrapper
// entry must expose the exact post-vector MSR, not the raw hardware-vector
// state and not the interrupted thread's original MSR.
[[nodiscard]] constexpr std::uint32_t os_exception_handler_msr(
    std::uint32_t saved_msr) noexcept {
    return hardware_exception_msr(saved_msr) |
           kMsrInstructionRelocation | kMsrDataRelocation;
}
inline constexpr std::uint32_t kExceptionHandlerTable = 0x80003000u;
inline constexpr std::uint32_t kExternalException = 4u;
inline constexpr std::uint32_t kFloatingPointUnavailableException = 7u;
inline constexpr std::uint32_t kDecrementerException = 8u;
inline constexpr std::uint32_t kExternalExceptionWrapper = 0x804A881Cu;
inline constexpr std::uint32_t kFloatingPointUnavailableHandler = 0x804A3C9Cu;
inline constexpr std::uint32_t kDecrementerExceptionWrapper = 0x804A259Cu;

struct TranslatedExceptionEntry {
    std::uint32_t exception{};
    std::uint32_t wrapper{};
};

// Exact identity of one in-flight lazy-FPU exception through its translated
// exception-7 RFI. The identity is retired before any asynchronous exception
// is arbitrated, even though the original host callback frame may still be
// live. This is deliberately a single fail-closed slot, not a permissive
// nesting stack.
class FpuRetryProvenance final {
public:
    [[nodiscard]] bool begin(
        std::uint32_t retry_pc,
        std::uint32_t retry_context) noexcept {
        if (active() || retry_pc == 0u || retry_context == 0u) {
            return false;
        }
        retry_pc_ = retry_pc;
        retry_context_ = retry_context;
        return true;
    }

    [[nodiscard]] bool active() const noexcept {
        return retry_pc_ != 0u || retry_context_ != 0u;
    }

    [[nodiscard]] bool matches(
        std::uint32_t retry_pc,
        std::uint32_t retry_context) const noexcept {
        return retry_pc_ == retry_pc && retry_context_ == retry_context &&
               retry_pc != 0u && retry_context != 0u;
    }

    [[nodiscard]] std::uint32_t retry_pc() const noexcept {
        return retry_pc_;
    }

    [[nodiscard]] std::uint32_t retry_context() const noexcept {
        return retry_context_;
    }

    void clear() noexcept {
        retry_pc_ = 0u;
        retry_context_ = 0u;
    }

private:
    std::uint32_t retry_pc_{};
    std::uint32_t retry_context_{};
};

inline constexpr TranslatedExceptionEntry kExternalExceptionEntry{
    kExternalException,
    kExternalExceptionWrapper};
inline constexpr TranslatedExceptionEntry kFloatingPointUnavailableExceptionEntry{
    kFloatingPointUnavailableException,
    kFloatingPointUnavailableHandler};
inline constexpr TranslatedExceptionEntry kDecrementerExceptionEntry{
    kDecrementerException,
    kDecrementerExceptionWrapper};

[[nodiscard]] constexpr bool exception_is_recoverable(
    std::uint32_t saved_msr) noexcept {
    return (saved_msr & kMsrRecoverableException) != 0u;
}

[[nodiscard]] constexpr std::uint32_t physical_context_pointer(
    std::uint32_t cached_context_pointer) noexcept {
    return cached_context_pointer & 0x3FFFFFFFu;
}

// Exact memory writes performed by RMGE01's copied low-memory
// OSExceptionVector before it selects an installed handler. The normal
// decrementer/external wrappers subsequently save r0-r2, r6-r31 and GQR1-7;
// the lazy-FPU handler deliberately does not. Therefore this vector boundary
// must not pre-save those fields or GQR0 on the FPU path.
inline void save_low_memory_exception_vector_context(
    const PpcContext& context,
    std::uint32_t resume_address,
    std::uint32_t guest_context,
    GuestMemoryV1* memory,
    const NativeServicesV1* services) {
    constexpr std::uint32_t kCrOffset = 0x80u;
    constexpr std::uint32_t kLrOffset = 0x84u;
    constexpr std::uint32_t kCtrOffset = 0x88u;
    constexpr std::uint32_t kXerOffset = 0x8Cu;
    constexpr std::uint32_t kSrr0Offset = 0x198u;
    constexpr std::uint32_t kSrr1Offset = 0x19Cu;
    constexpr std::uint32_t kStateOffset = 0x1A2u;
    constexpr std::uint16_t kExceptionState = 2u;

    for (std::uint32_t index = 3u; index <= 5u; ++index) {
        guest_store_u32(
            memory,
            guest_context + index * sizeof(std::uint32_t),
            context.gpr[index],
            services,
            resume_address);
    }
    guest_store_u32(
        memory, guest_context + kCrOffset, context.cr, services, resume_address);
    guest_store_u32(
        memory, guest_context + kLrOffset, context.lr, services, resume_address);
    guest_store_u32(
        memory, guest_context + kCtrOffset, context.ctr, services, resume_address);
    guest_store_u32(
        memory, guest_context + kXerOffset, context.xer, services, resume_address);
    guest_store_u32(
        memory,
        guest_context + kSrr0Offset,
        resume_address,
        services,
        resume_address);
    guest_store_u32(
        memory,
        guest_context + kSrr1Offset,
        hardware_exception_srr1(context.msr),
        services,
        resume_address);
    const std::uint16_t state = guest_load_u16(
        memory, guest_context + kStateOffset, services, resume_address);
    guest_store_u16(
        memory,
        guest_context + kStateOffset,
        static_cast<std::uint16_t>(state | kExceptionState),
        services,
        resume_address);
}

inline void enter_translated_exception_wrapper(
    PpcContext& context,
    TranslatedExceptionEntry entry,
    std::uint32_t guest_context) noexcept {
    constexpr std::uint32_t kSprg0 = 272u;
    constexpr std::uint32_t kSrr0 = 26u;
    constexpr std::uint32_t kSrr1 = 27u;
    const std::uint32_t saved_msr = context.msr;
    const std::uint32_t interrupted_r4 = context.gpr[4];
    const std::uint32_t handler_msr = os_exception_handler_msr(saved_msr);

    // Exact final state of RMGE01's copied low-memory OSExceptionVector after
    // its second RFI enters the installed DOL handler. The original r3/r4/r5,
    // CR, SRR0, and SRR1 were already written to OSContext by
    // save_exception_context before this helper runs.
    context.spr[kSprg0] = interrupted_r4;
    context.spr[kSrr0] = entry.wrapper;
    context.spr[kSrr1] = handler_msr;
    context.msr = handler_msr;
    context.pc = entry.wrapper;
    context.gpr[3] = entry.exception;
    context.gpr[4] = guest_context;
    context.gpr[5] = entry.wrapper;
    update_cr0(&context, saved_msr & kMsrRecoverableException);
}

// Native AOT calls cannot preserve a translated interrupt handler's C++ call
// frames across an OSLoadContext handoff.  Name the source-specific linearity
// policy so each interrupt keeps the same behavior when a new source is added.
// IPC has historically deferred every in-handler handoff; VI and DI defer only
// cross-context handoffs.  AI follows the latter rule so a nested self-resume
// can use the established self-resume path while a callback wakeup cannot
// abandon the translated __AIDHandler before its epilogue runs.
enum class SchedulerHandoffSource : std::uint8_t {
    Ipc,
    Vi,
    Di,
    Ai,
};

[[nodiscard]] constexpr bool should_defer_scheduler_handoff(
    SchedulerHandoffSource source,
    bool policy_enabled,
    bool source_dispatching,
    bool nested_self_resume) noexcept {
    if (!policy_enabled || !source_dispatching) {
        return false;
    }

    switch (source) {
        case SchedulerHandoffSource::Ipc:
            return true;
        case SchedulerHandoffSource::Vi:
        case SchedulerHandoffSource::Di:
        case SchedulerHandoffSource::Ai:
            return !nested_self_resume;
    }
    return false;
}

// AI DMA arrives through Broadway's external-interrupt exception. Keep this
// source-specific policy named and testable so the dispatch site cannot drift
// back to running __AIDHandler with MSR[EE] still set.
[[nodiscard]] constexpr std::uint32_t ai_dma_msr_clear_bits() noexcept {
    return kMsrExternalInterruptEnable;
}

inline void enter_guest_handler(
    PpcContext& context,
    std::uint32_t msr_clear_bits) noexcept {
    context.msr &= ~msr_clear_bits;
}

// Interrupt handlers execute in the dispatcher's PpcContext storage. Restore
// its outer register snapshot after the guest returns, while retaining timer
// mutations made during the handler exactly as native_runtime.cpp has always
// required for monotonic time-base/decrementer behavior.
inline void restore_outer_context(
    PpcContext& context,
    const PpcContext& saved) noexcept {
    const PpcTimerState timer_state = capture_timer_state(&context);
    context = saved;
    restore_timer_state(&context, timer_state);
}

}  // namespace galaxy::interrupt
