#pragma once

#include "galaxy/native_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace galaxy::scheduler {

// The translated OSLoadContext call site owns this token. The RFI boundary may
// inspect it, but only the call site consumes it while unwinding. This keeps
// normal and exceptional exits on the same exactly-once path.
struct ContextTransferToken {
    std::uint32_t target_context{};
    std::uint32_t call_pc{};

    friend constexpr bool operator==(
        const ContextTransferToken&,
        const ContextTransferToken&) noexcept = default;
};

class PendingContextTransfer final {
public:
    [[nodiscard]] bool begin(ContextTransferToken token) noexcept {
        if (pending_.has_value() || cleanup_failure_.has_value()) {
            return false;
        }
        pending_ = token;
        return true;
    }

    [[nodiscard]] const ContextTransferToken* pending() const noexcept {
        return pending_.has_value() ? &*pending_ : nullptr;
    }

    [[nodiscard]] bool consume_exact(ContextTransferToken token) noexcept {
        if (!pending_.has_value() || *pending_ != token) {
            return false;
        }
        pending_.reset();
        return true;
    }

    // Sticky failure: a control-transfer catch must reject continuation before
    // guest work resumes. Failed cleanup never erases a different pending token.
    [[nodiscard]] const ContextTransferToken* cleanup_failure() const noexcept {
        return cleanup_failure_.has_value() ? &*cleanup_failure_ : nullptr;
    }

private:
    friend class ScopedContextTransfer;
    [[nodiscard]] bool finish_owner(ContextTransferToken token) noexcept {
        if (consume_exact(token)) {
            return true;
        }
        if (!cleanup_failure_.has_value()) {
            cleanup_failure_ = token;
        }
        return false;
    }
    std::optional<ContextTransferToken> pending_;
    std::optional<ContextTransferToken> cleanup_failure_;
};

// Construct only after begin(token) succeeds. This owns exactly the translated
// OSLoadContext call boundary: RFI can inspect its token until stack unwinding
// reaches this scope. No catch/rethrow or exception from a destructor is needed.
class ScopedContextTransfer final {
public:
    ScopedContextTransfer(PendingContextTransfer& pending,
                          ContextTransferToken token) noexcept
        : pending_(pending), token_(token) {}
    ~ScopedContextTransfer() noexcept { (void)finish(); }
    ScopedContextTransfer(const ScopedContextTransfer&) = delete;
    ScopedContextTransfer& operator=(const ScopedContextTransfer&) = delete;

    [[nodiscard]] bool finish() noexcept {
        if (!active_) {
            return false;
        }
        active_ = false;
        return pending_.finish_owner(token_);
    }

private:
    PendingContextTransfer& pending_;
    ContextTransferToken token_;
    bool active_{true};
};

// The statically recompiled instruction stream owns all exception-prologue and
// OSLoadContext register traffic. The host boundary implements only the two
// architectural effects of Broadway RFI: merge the saved SRR1 subset into MSR,
// clear MSR[13], and resume at SRR0. No OSContext memory is consulted here
// because most RMGE01 RFI sites are not OSLoadContext.
[[nodiscard]] inline std::uint32_t apply_rfi(
    galaxy::PpcContext& context) noexcept {
    constexpr std::uint32_t kRestoredMsrMask = 0x87C0FFFFu;
    constexpr std::uint32_t kClearMsr13Mask = 0xFFFBFFFFu;
    const std::uint32_t resume_pc = context.spr[26];
    context.msr =
        ((context.msr & ~kRestoredMsrMask) |
         (context.spr[27] & kRestoredMsrMask)) &
        kClearMsr13Mask;
    context.pc = resume_pc;
    return resume_pc;
}

// Broadway may place a physical MEM1/MEM2 effective address in SRR0 while the
// install-time module exposes the same instruction through its cached virtual
// alias. Address translation belongs at the static-module binding boundary,
// not in apply_rfi: the architectural RFI above must preserve SRR0 exactly.
// Prefer an exact translated address when one exists, otherwise accept the
// cached alias only when that exact alias was emitted by the recompiler. An
// unknown address is returned unchanged so the caller can hard-fail it.
template <typename HasExactContinuation>
[[nodiscard]] std::uint32_t resolve_static_executable_address(
    std::uint32_t address,
    std::uint32_t mem1_size,
    std::uint32_t mem2_size,
    HasExactContinuation&& has_exact_continuation) {
    address &= 0xFFFFFFFCu;
    if (has_exact_continuation(address)) {
        return address;
    }

    constexpr std::uint32_t kPhysicalMem2Base = 0x10000000u;
    const bool physical_mem1 = address < mem1_size;
    const bool physical_mem2 =
        address >= kPhysicalMem2Base &&
        address - kPhysicalMem2Base < mem2_size;
    if (physical_mem1 || physical_mem2) {
        const std::uint32_t cached_alias = address | 0x80000000u;
        if (has_exact_continuation(cached_alias)) {
            return cached_alias;
        }
    }
    return address;
}

// OSThread records stackEnd as the low inclusive address and stackBase as the
// high exclusive address. Wii titles may allocate stacks in cached MEM1 or
// cached MEM2. Keep the two aliases distinct and reject physical, uncached,
// cross-region, empty, and overflowed spans before consulting host mappings.
[[nodiscard]] constexpr bool is_cached_guest_ram_stack_span(
    std::uint32_t stack_base,
    std::uint32_t stack_end,
    std::uint32_t mem1_size,
    std::uint32_t mem2_size) noexcept {
    if (stack_end >= stack_base) {
        return false;
    }
    constexpr std::uint32_t kCachedMem1Base = 0x80000000u;
    constexpr std::uint32_t kCachedMem2Base = 0x90000000u;
    const std::uint64_t mem1_limit =
        static_cast<std::uint64_t>(kCachedMem1Base) + mem1_size;
    const std::uint64_t mem2_limit =
        static_cast<std::uint64_t>(kCachedMem2Base) + mem2_size;
    const bool cached_mem1 =
        stack_end >= kCachedMem1Base &&
        static_cast<std::uint64_t>(stack_base) <= mem1_limit;
    const bool cached_mem2 =
        stack_end >= kCachedMem2Base &&
        static_cast<std::uint64_t>(stack_base) <= mem2_limit;
    return cached_mem1 || cached_mem2;
}

[[nodiscard]] constexpr bool guest_stack_span_contains(
    std::uint32_t stack_base,
    std::uint32_t stack_end,
    std::uint32_t address) noexcept {
    return stack_end < stack_base &&
           address >= stack_end && address < stack_base;
}

enum class FlatContinuationStopReason : std::uint8_t {
    MissingExactContinuation,
    StackTopSentinel,
};

// OSSleepThread is itself statically translated. The host may reject an
// invalid queue pointer, but every valid queue must enter that translated
// routine so the guest owns enqueueing, SelectThread, the idle context, and
// the eventual wakeup. There is deliberately no host-wakeup disposition.
enum class GuestSleepQueueDisposition : std::uint8_t {
    RunTranslatedScheduler,
    RejectNullQueue,
    RejectMisalignedQueue,
    RejectUnmappedQueue,
};

// Evidence collected without mutating guest state at the exact translated
// OSSleepThread boundary. OSSetCurrentContext writes D4 and its physical C0
// alias together. SelectThread installs the running OSThread in E4 before it
// installs that thread's OSContext in D4. An exception context may live inside
// the same thread's stack, so context_owner_thread records that exact case.
struct GuestSleepIdentityEvidence {
    std::uint32_t current_context{};
    std::uint32_t physical_context{};
    std::uint32_t current_thread{};
    std::uint32_t context_owner_thread{};
    std::uint32_t r1_owner_thread{};
    std::uint16_t current_thread_state{};
    bool current_context_is_mapped{};
    bool current_thread_is_mapped{};
    bool current_thread_is_active{};
};

enum class GuestSleepIdentityDisposition : std::uint8_t {
    Valid,
    RejectNullCurrentContext,
    RejectUnmappedCurrentContext,
    RejectPhysicalContextAlias,
    RejectNullCurrentThread,
    RejectUnmappedCurrentThread,
    RejectInactiveCurrentThread,
    RejectNonRunningCurrentThread,
    RejectContextOwnerMismatch,
    RejectStackOwnerMismatch,
};

// Evidence at an architectural exception boundary. The exception vector saves
// into D4, while C0 is OSSetCurrentContext's physical alias. A normal running
// thread therefore requires D4 (or a stack-local OSContext containing D4), E4,
// and the executing r1 to name one active OSThread. SelectThread installs
// IdleContext in D4/C0 and null in E4 before enabling interrupts. An exception
// taken in that window may then install one or more of RMGE01's exact
// stack-local callback contexts while E4 remains null. That second case is
// accepted only with the complete, role-validated OSSetCurrentContext lineage
// rooted at IdleContext and matching nonzero context/r1 owners.
struct GuestExceptionIdentityEvidence {
    std::uint32_t requested_context{};
    std::uint32_t current_context{};
    std::uint32_t physical_context{};
    std::uint32_t current_thread{};
    std::uint32_t context_owner_thread{};
    std::uint32_t r1_owner_thread{};
    std::uint16_t current_thread_state{};
    bool current_context_is_mapped{};
    bool current_context_is_idle{};
    bool current_thread_is_mapped{};
    bool current_thread_is_active{};
    bool current_context_has_idle_temporary_lineage{};
};

enum class GuestExceptionIdentityDisposition : std::uint8_t {
    ValidThread,
    ValidIdle,
    ValidIdleTemporary,
    RejectNullCurrentContext,
    RejectMisalignedCurrentContext,
    RejectCurrentContextChanged,
    RejectUnmappedCurrentContext,
    RejectPhysicalContextAlias,
    RejectIdleCurrentThread,
    RejectIdleTemporaryCurrentThread,
    RejectNullCurrentThread,
    RejectUnmappedCurrentThread,
    RejectInactiveCurrentThread,
    RejectNonRunningCurrentThread,
    RejectContextOwnerMismatch,
    RejectStackOwnerMismatch,
};

[[nodiscard]] constexpr GuestExceptionIdentityDisposition
classify_guest_exception_identity(
    const GuestExceptionIdentityEvidence& evidence) noexcept {
    constexpr std::uint16_t kRunningThreadState = 2u;
    if (evidence.requested_context == 0u || evidence.current_context == 0u) {
        return GuestExceptionIdentityDisposition::RejectNullCurrentContext;
    }
    if ((evidence.current_context & 7u) != 0u) {
        return GuestExceptionIdentityDisposition::
            RejectMisalignedCurrentContext;
    }
    if (evidence.requested_context != evidence.current_context) {
        return GuestExceptionIdentityDisposition::RejectCurrentContextChanged;
    }
    if (!evidence.current_context_is_mapped) {
        return GuestExceptionIdentityDisposition::RejectUnmappedCurrentContext;
    }
    if (evidence.physical_context !=
        (evidence.current_context & 0x3FFFFFFFu)) {
        return GuestExceptionIdentityDisposition::RejectPhysicalContextAlias;
    }
    if (evidence.current_context_is_idle) {
        return evidence.current_thread == 0u
            ? GuestExceptionIdentityDisposition::ValidIdle
            : GuestExceptionIdentityDisposition::RejectIdleCurrentThread;
    }
    if (evidence.current_context_has_idle_temporary_lineage) {
        if (evidence.current_thread != 0u) {
            return GuestExceptionIdentityDisposition::
                RejectIdleTemporaryCurrentThread;
        }
        if (evidence.context_owner_thread == 0u) {
            return GuestExceptionIdentityDisposition::
                RejectContextOwnerMismatch;
        }
        return evidence.r1_owner_thread == evidence.context_owner_thread
            ? GuestExceptionIdentityDisposition::ValidIdleTemporary
            : GuestExceptionIdentityDisposition::RejectStackOwnerMismatch;
    }
    if (evidence.current_thread == 0u) {
        return GuestExceptionIdentityDisposition::RejectNullCurrentThread;
    }
    if (!evidence.current_thread_is_mapped) {
        return GuestExceptionIdentityDisposition::RejectUnmappedCurrentThread;
    }
    if (!evidence.current_thread_is_active) {
        return GuestExceptionIdentityDisposition::RejectInactiveCurrentThread;
    }
    if (evidence.current_thread_state != kRunningThreadState) {
        return GuestExceptionIdentityDisposition::RejectNonRunningCurrentThread;
    }
    if (evidence.current_context != evidence.current_thread &&
        evidence.context_owner_thread != evidence.current_thread) {
        return GuestExceptionIdentityDisposition::RejectContextOwnerMismatch;
    }
    if (evidence.r1_owner_thread != evidence.current_thread) {
        return GuestExceptionIdentityDisposition::RejectStackOwnerMismatch;
    }
    return GuestExceptionIdentityDisposition::ValidThread;
}

// RMGE01 has one translated call site for OSSaveContext: SelectThread at
// 0x804AB308. It first proves D4 == E4, then passes E4 unchanged in r3 after
// checking OS_CONTEXT_STATE_EXC is clear. The thread may already be ready,
// waiting, or terminating, so its scheduling state is intentionally not part
// of this contract. r1 may be on an OSSwitchFiber stack, whose exact backchain
// is resolved before this evidence is classified.
struct GuestSaveContextIdentityEvidence {
    std::uint32_t requested_context{};
    std::uint32_t current_context{};
    std::uint32_t physical_context{};
    std::uint32_t current_thread{};
    std::uint32_t r1_owner_thread{};
    std::uint16_t requested_context_state{};
    bool requested_context_is_mapped{};
    bool current_thread_is_mapped{};
    bool current_thread_is_active{};
};

enum class GuestSaveContextIdentityDisposition : std::uint8_t {
    Valid,
    RejectNullRequestedContext,
    RejectUnmappedRequestedContext,
    RejectCurrentContextMismatch,
    RejectPhysicalContextAlias,
    RejectCurrentThreadMismatch,
    RejectUnmappedCurrentThread,
    RejectInactiveCurrentThread,
    RejectStackOwnerMismatch,
    RejectExceptionState,
};

[[nodiscard]] constexpr GuestSaveContextIdentityDisposition
classify_guest_save_context_identity(
    const GuestSaveContextIdentityEvidence& evidence) noexcept {
    constexpr std::uint16_t kExceptionContextState = 2u;
    if (evidence.requested_context == 0u) {
        return GuestSaveContextIdentityDisposition::
            RejectNullRequestedContext;
    }
    if (!evidence.requested_context_is_mapped) {
        return GuestSaveContextIdentityDisposition::
            RejectUnmappedRequestedContext;
    }
    if (evidence.current_context != evidence.requested_context) {
        return GuestSaveContextIdentityDisposition::
            RejectCurrentContextMismatch;
    }
    if (evidence.physical_context !=
        (evidence.requested_context & 0x3FFFFFFFu)) {
        return GuestSaveContextIdentityDisposition::
            RejectPhysicalContextAlias;
    }
    if (evidence.current_thread != evidence.requested_context) {
        return GuestSaveContextIdentityDisposition::
            RejectCurrentThreadMismatch;
    }
    if (!evidence.current_thread_is_mapped) {
        return GuestSaveContextIdentityDisposition::
            RejectUnmappedCurrentThread;
    }
    if (!evidence.current_thread_is_active) {
        return GuestSaveContextIdentityDisposition::
            RejectInactiveCurrentThread;
    }
    if (evidence.r1_owner_thread != evidence.requested_context) {
        return GuestSaveContextIdentityDisposition::RejectStackOwnerMismatch;
    }
    if ((evidence.requested_context_state & kExceptionContextState) != 0u) {
        return GuestSaveContextIdentityDisposition::RejectExceptionState;
    }
    return GuestSaveContextIdentityDisposition::Valid;
}

// RMGE01 has exactly thirty direct OSSetCurrentContext call boundaries. The
// return LR uniquely describes the architectural role of every call: thirteen
// stack-local callback-context installs and their exact restores, plus four
// SDK scheduler/fatal/bootstrap transitions. Unknown callers hard-fail instead
// of inferring a replacement context from the native frame owner.
enum class GuestSetCurrentContextRoleKind : std::uint8_t {
    TemporaryInstall,
    TemporaryRestore,
    FatalContext,
    BootstrapThread,
    SchedulerIdle,
    SchedulerThread,
};

struct GuestSetCurrentContextRole {
    std::uint32_t return_lr{};
    GuestSetCurrentContextRoleKind kind{};
    // TemporaryInstall: exact offset from the caller's live r1.
    // Static special roles: exact requested context.
    std::uint32_t argument{};
    // Temporary install/restore roles name one another exactly.
    std::uint32_t paired_return_lr{};

    friend constexpr bool operator==(
        const GuestSetCurrentContextRole&,
        const GuestSetCurrentContextRole&) noexcept = default;
};

inline constexpr std::array<GuestSetCurrentContextRole, 30u>
    kRmge01SetCurrentContextRoles{{
        {0x804965ECu, GuestSetCurrentContextRoleKind::TemporaryInstall,
         0x8u, 0x804968E4u},
        {0x804968E4u, GuestSetCurrentContextRoleKind::TemporaryRestore,
         0u, 0x804965ECu},
        {0x804A2548u, GuestSetCurrentContextRoleKind::TemporaryInstall,
         0x8u, 0x804A256Cu},
        {0x804A256Cu, GuestSetCurrentContextRoleKind::TemporaryRestore,
         0u, 0x804A2548u},
        {0x804A3B54u, GuestSetCurrentContextRoleKind::TemporaryInstall,
         0x8u, 0x804A3C28u},
        {0x804A3C28u, GuestSetCurrentContextRoleKind::TemporaryRestore,
         0u, 0x804A3B54u},
        {0x804A6C58u, GuestSetCurrentContextRoleKind::FatalContext,
         0x80650540u, 0u},
        {0x804AAC88u, GuestSetCurrentContextRoleKind::BootstrapThread,
         0x80650878u, 0u},
        {0x804AB354u, GuestSetCurrentContextRoleKind::SchedulerIdle,
         0x80650C90u, 0u},
        {0x804AB410u, GuestSetCurrentContextRoleKind::SchedulerThread,
         0u, 0u},
        {0x804AF370u, GuestSetCurrentContextRoleKind::TemporaryInstall,
         0x8u, 0x804AF394u},
        {0x804AF394u, GuestSetCurrentContextRoleKind::TemporaryRestore,
         0u, 0x804AF370u},
        {0x804AF58Cu, GuestSetCurrentContextRoleKind::TemporaryInstall,
         0x8u, 0x804AF5B0u},
        {0x804AF5B0u, GuestSetCurrentContextRoleKind::TemporaryRestore,
         0u, 0x804AF58Cu},
        {0x804AF64Cu, GuestSetCurrentContextRoleKind::TemporaryInstall,
         0x8u, 0x804AF678u},
        {0x804AF678u, GuestSetCurrentContextRoleKind::TemporaryRestore,
         0u, 0x804AF64Cu},
        {0x804B16E4u, GuestSetCurrentContextRoleKind::TemporaryInstall,
         0x10u, 0x804B1748u},
        {0x804B1748u, GuestSetCurrentContextRoleKind::TemporaryRestore,
         0u, 0x804B16E4u},
        {0x804B1768u, GuestSetCurrentContextRoleKind::TemporaryInstall,
         0x10u, 0x804B1B88u},
        {0x804B1B88u, GuestSetCurrentContextRoleKind::TemporaryRestore,
         0u, 0x804B1768u},
        {0x804B8424u, GuestSetCurrentContextRoleKind::TemporaryInstall,
         0x8u, 0x804B8440u},
        {0x804B8440u, GuestSetCurrentContextRoleKind::TemporaryRestore,
         0u, 0x804B8424u},
        {0x804BA2C8u, GuestSetCurrentContextRoleKind::TemporaryInstall,
         0x8u, 0x804BA2E8u},
        {0x804BA2E8u, GuestSetCurrentContextRoleKind::TemporaryRestore,
         0u, 0x804BA2C8u},
        {0x804BA39Cu, GuestSetCurrentContextRoleKind::TemporaryInstall,
         0x8u, 0x804BA3B8u},
        {0x804BA3B8u, GuestSetCurrentContextRoleKind::TemporaryRestore,
         0u, 0x804BA39Cu},
        {0x804C817Cu, GuestSetCurrentContextRoleKind::TemporaryInstall,
         0x8u, 0x804C81D4u},
        {0x804C81D4u, GuestSetCurrentContextRoleKind::TemporaryRestore,
         0u, 0x804C817Cu},
        {0x804D213Cu, GuestSetCurrentContextRoleKind::TemporaryInstall,
         0x8u, 0x804D2160u},
        {0x804D2160u, GuestSetCurrentContextRoleKind::TemporaryRestore,
         0u, 0x804D213Cu},
    }};

[[nodiscard]] constexpr const GuestSetCurrentContextRole*
find_guest_set_current_context_role(std::uint32_t return_lr) noexcept {
    for (const auto& role : kRmge01SetCurrentContextRoles) {
        if (role.return_lr == return_lr) {
            return &role;
        }
    }
    return nullptr;
}

[[nodiscard]] constexpr bool
guest_set_current_context_role_table_is_exact() noexcept {
    std::size_t installs = 0u;
    std::size_t restores = 0u;
    std::size_t specials = 0u;
    for (std::size_t i = 0u;
         i < kRmge01SetCurrentContextRoles.size();
         ++i) {
        const auto& role = kRmge01SetCurrentContextRoles[i];
        if (role.return_lr == 0u) {
            return false;
        }
        for (std::size_t j = i + 1u;
             j < kRmge01SetCurrentContextRoles.size();
             ++j) {
            if (role.return_lr ==
                kRmge01SetCurrentContextRoles[j].return_lr) {
                return false;
            }
        }
        if (role.kind ==
            GuestSetCurrentContextRoleKind::TemporaryInstall) {
            ++installs;
            if ((role.argument != 0x8u && role.argument != 0x10u) ||
                role.paired_return_lr == 0u) {
                return false;
            }
            const auto* peer = find_guest_set_current_context_role(
                role.paired_return_lr);
            if (peer == nullptr ||
                peer->kind !=
                    GuestSetCurrentContextRoleKind::TemporaryRestore ||
                peer->paired_return_lr != role.return_lr) {
                return false;
            }
        } else if (role.kind ==
                   GuestSetCurrentContextRoleKind::TemporaryRestore) {
            ++restores;
            const auto* peer = find_guest_set_current_context_role(
                role.paired_return_lr);
            if (role.argument != 0u || peer == nullptr ||
                peer->kind !=
                    GuestSetCurrentContextRoleKind::TemporaryInstall ||
                peer->paired_return_lr != role.return_lr) {
                return false;
            }
        } else {
            ++specials;
            if (role.paired_return_lr != 0u) {
                return false;
            }
        }
    }
    return installs == 13u && restores == 13u && specials == 4u;
}

static_assert(guest_set_current_context_role_table_is_exact());

struct GuestSetCurrentContextToken {
    std::uint32_t temporary_context{};
    std::uint32_t previous_context{};
    std::uint32_t install_return_lr{};
    std::uint32_t restore_return_lr{};

    friend constexpr bool operator==(
        const GuestSetCurrentContextToken&,
        const GuestSetCurrentContextToken&) noexcept = default;
};

class PendingGuestSetCurrentContexts final {
public:
    static constexpr std::size_t kCapacity = 64u;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return size_ == 0u;
    }

    [[nodiscard]] constexpr bool full() const noexcept {
        return size_ == entries_.size();
    }

    [[nodiscard]] constexpr const GuestSetCurrentContextToken* top()
        const noexcept {
        return empty() ? nullptr : &entries_[size_ - 1u];
    }

    [[nodiscard]] constexpr bool push(
        GuestSetCurrentContextToken token) noexcept {
        if (full()) {
            return false;
        }
        entries_[size_++] = token;
        return true;
    }

    [[nodiscard]] constexpr bool consume_exact(
        GuestSetCurrentContextToken token) noexcept {
        if (empty() || entries_[size_ - 1u] != token) {
            return false;
        }
        --size_;
        return true;
    }

    // Prove that every live temporary OSSetCurrentContext transition belongs
    // to one uninterrupted LIFO chain from current_context back to
    // root_context. Tokens are normally created only after the call boundary
    // has passed classify_guest_set_current_context, but revalidate the exact
    // install/restore role pair here so exception identity never trusts stale,
    // malformed, or fabricated bookkeeping.
    [[nodiscard]] constexpr bool has_exact_lineage(
        std::uint32_t current_context,
        std::uint32_t root_context) const noexcept {
        constexpr std::uint32_t kContextAlignment = 8u;
        if (empty() || current_context == 0u || root_context == 0u ||
            current_context == root_context ||
            (current_context & (kContextAlignment - 1u)) != 0u ||
            (root_context & (kContextAlignment - 1u)) != 0u) {
            return false;
        }

        std::uint32_t expected_temporary = current_context;
        for (std::size_t i = size_; i > 0u; --i) {
            const auto& token = entries_[i - 1u];
            if (token.temporary_context != expected_temporary ||
                token.previous_context == 0u ||
                token.temporary_context == token.previous_context ||
                (token.temporary_context & (kContextAlignment - 1u)) != 0u ||
                (token.previous_context & (kContextAlignment - 1u)) != 0u) {
                return false;
            }

            const auto* install = find_guest_set_current_context_role(
                token.install_return_lr);
            const auto* restore = find_guest_set_current_context_role(
                token.restore_return_lr);
            if (install == nullptr || restore == nullptr ||
                install->kind !=
                    GuestSetCurrentContextRoleKind::TemporaryInstall ||
                restore->kind !=
                    GuestSetCurrentContextRoleKind::TemporaryRestore ||
                install->paired_return_lr != token.restore_return_lr ||
                restore->paired_return_lr != token.install_return_lr) {
                return false;
            }
            expected_temporary = token.previous_context;
        }
        return expected_temporary == root_context;
    }

private:
    std::array<GuestSetCurrentContextToken, kCapacity> entries_{};
    std::size_t size_{};
};

struct GuestSetCurrentContextEvidence {
    std::uint32_t return_lr{};
    std::uint32_t requested_context{};
    std::uint32_t current_context{};
    std::uint32_t physical_context{};
    std::uint32_t current_thread{};
    std::uint32_t r1{};
    std::uint16_t current_thread_state{};
    bool requested_context_is_mapped{};
    bool current_context_is_mapped{};
    bool current_thread_is_mapped{};
    bool current_thread_is_active{};
    bool token_stack_is_full{};
};

enum class GuestSetCurrentContextDisposition : std::uint8_t {
    ValidTemporaryInstall,
    ValidTemporaryRestore,
    ValidFatalContext,
    ValidBootstrapThread,
    ValidSchedulerIdle,
    ValidSchedulerThread,
    RejectUnknownRole,
    RejectNullRequestedContext,
    RejectMisalignedRequestedContext,
    RejectUnmappedRequestedContext,
    RejectPhysicalContextAlias,
    RejectUnmappedCurrentContext,
    RejectBootstrapPreviousContext,
    RejectTemporaryAddress,
    RejectTokenOverflow,
    RejectRestoreWithoutToken,
    RejectRestoreRole,
    RejectRestoreCurrentContext,
    RejectRestoreTarget,
    RejectSpecialContext,
    RejectIdleCurrentThread,
    RejectThreadMismatch,
    RejectUnmappedCurrentThread,
    RejectInactiveCurrentThread,
    RejectNonRunningCurrentThread,
};

[[nodiscard]] constexpr GuestSetCurrentContextDisposition
classify_guest_set_current_context(
    const GuestSetCurrentContextEvidence& evidence,
    const GuestSetCurrentContextToken* pending) noexcept {
    constexpr std::uint32_t kContextAlignment = 8u;
    constexpr std::uint16_t kRunningThreadState = 2u;
    const auto* role =
        find_guest_set_current_context_role(evidence.return_lr);
    if (role == nullptr) {
        return GuestSetCurrentContextDisposition::RejectUnknownRole;
    }
    if (evidence.requested_context == 0u) {
        return GuestSetCurrentContextDisposition::
            RejectNullRequestedContext;
    }
    if ((evidence.requested_context & (kContextAlignment - 1u)) != 0u) {
        return GuestSetCurrentContextDisposition::
            RejectMisalignedRequestedContext;
    }
    if (!evidence.requested_context_is_mapped) {
        return GuestSetCurrentContextDisposition::
            RejectUnmappedRequestedContext;
    }
    if (evidence.physical_context !=
        (evidence.current_context & 0x3FFFFFFFu)) {
        return GuestSetCurrentContextDisposition::RejectPhysicalContextAlias;
    }
    if (role->kind ==
        GuestSetCurrentContextRoleKind::BootstrapThread) {
        if (evidence.current_context != 0u) {
            return GuestSetCurrentContextDisposition::
                RejectBootstrapPreviousContext;
        }
    } else if (evidence.current_context == 0u ||
               (evidence.current_context &
                (kContextAlignment - 1u)) != 0u ||
               !evidence.current_context_is_mapped) {
        return GuestSetCurrentContextDisposition::
            RejectUnmappedCurrentContext;
    }

    switch (role->kind) {
        case GuestSetCurrentContextRoleKind::TemporaryInstall:
            if (evidence.requested_context != evidence.r1 + role->argument) {
                return GuestSetCurrentContextDisposition::
                    RejectTemporaryAddress;
            }
            if (evidence.token_stack_is_full) {
                return GuestSetCurrentContextDisposition::RejectTokenOverflow;
            }
            return GuestSetCurrentContextDisposition::ValidTemporaryInstall;
        case GuestSetCurrentContextRoleKind::TemporaryRestore:
            if (pending == nullptr) {
                return GuestSetCurrentContextDisposition::
                    RejectRestoreWithoutToken;
            }
            if (pending->restore_return_lr != evidence.return_lr ||
                pending->install_return_lr != role->paired_return_lr) {
                return GuestSetCurrentContextDisposition::RejectRestoreRole;
            }
            if (pending->temporary_context != evidence.current_context) {
                return GuestSetCurrentContextDisposition::
                    RejectRestoreCurrentContext;
            }
            if (pending->previous_context != evidence.requested_context) {
                return GuestSetCurrentContextDisposition::RejectRestoreTarget;
            }
            return GuestSetCurrentContextDisposition::ValidTemporaryRestore;
        case GuestSetCurrentContextRoleKind::FatalContext:
            return evidence.requested_context == role->argument
                ? GuestSetCurrentContextDisposition::ValidFatalContext
                : GuestSetCurrentContextDisposition::RejectSpecialContext;
        case GuestSetCurrentContextRoleKind::BootstrapThread:
            if (evidence.requested_context != role->argument) {
                return GuestSetCurrentContextDisposition::RejectSpecialContext;
            }
            return evidence.current_thread == 0u
                ? GuestSetCurrentContextDisposition::ValidBootstrapThread
                : GuestSetCurrentContextDisposition::RejectIdleCurrentThread;
        case GuestSetCurrentContextRoleKind::SchedulerIdle:
            if (evidence.requested_context != role->argument) {
                return GuestSetCurrentContextDisposition::RejectSpecialContext;
            }
            return evidence.current_thread == 0u
                ? GuestSetCurrentContextDisposition::ValidSchedulerIdle
                : GuestSetCurrentContextDisposition::RejectIdleCurrentThread;
        case GuestSetCurrentContextRoleKind::SchedulerThread:
            if (evidence.requested_context != evidence.current_thread) {
                return GuestSetCurrentContextDisposition::RejectThreadMismatch;
            }
            if (!evidence.current_thread_is_mapped) {
                return GuestSetCurrentContextDisposition::
                    RejectUnmappedCurrentThread;
            }
            if (!evidence.current_thread_is_active) {
                return GuestSetCurrentContextDisposition::
                    RejectInactiveCurrentThread;
            }
            return evidence.current_thread_state == kRunningThreadState
                ? GuestSetCurrentContextDisposition::ValidSchedulerThread
                : GuestSetCurrentContextDisposition::
                      RejectNonRunningCurrentThread;
    }
    return GuestSetCurrentContextDisposition::RejectUnknownRole;
}

// Invocation-local roster evidence. Publish only after a complete ordered scan
// of ordinary RAM, and discard before any callback or guest reentry. This is
// deliberately not a cache across scheduling boundaries or guest writes.
class GuestStackOwnerMemo final {
public:
    void append(std::uint32_t thread, std::uint32_t base,
                std::uint32_t end) noexcept {
        if (!enabled_) return;
        if (count_ == spans_.size()) { disable(); return; }
        spans_[count_++] = {thread, base, end};
    }
    void finish() noexcept { complete_ = enabled_; }
    void disable() noexcept { enabled_ = false; complete_ = false; }
    [[nodiscard]] bool complete() const noexcept { return complete_; }
    [[nodiscard]] std::uint32_t owner(std::uint32_t r1) const noexcept {
        if (!complete_) return 0u;
        for (std::size_t i = 0; i < count_; ++i) {
            const auto& span = spans_[i];
            if (guest_stack_span_contains(span.base, span.end, r1)) {
                return span.thread;
            }
        }
        return 0u;
    }
private:
    struct Span { std::uint32_t thread, base, end; };
    std::array<Span, 64> spans_; // Only the initialized prefix is consulted.
    std::size_t count_{};
    bool enabled_{true};
    bool complete_{};
};

// OSSwitchFiber/OSSwitchFiberEx preserve the displaced r1 in the backchain at
// the root of the alternate stack. Resolve that exact ABI chain iteratively so
// a sleep reached from a translated fiber can still prove which OSThread owns
// it. Unknown, malformed, cyclic, and over-depth chains fail closed with 0.
template <typename DirectOwner, typename ReadPreviousR1>
[[nodiscard]] constexpr std::uint32_t resolve_guest_stack_owner(
    std::uint32_t r1,
    DirectOwner&& direct_owner,
    ReadPreviousR1&& read_previous_r1) {
    constexpr std::uint32_t kStackAlignment = 8u;
    constexpr std::uint32_t kMaxBackchainFrames = 256u;
    std::uint32_t frame = r1;
    for (std::uint32_t depth = 0u;
         depth < kMaxBackchainFrames;
         ++depth) {
        if (frame == 0u || (frame & (kStackAlignment - 1u)) != 0u) {
            return 0u;
        }
        const std::uint32_t owner = direct_owner(frame);
        if (owner != 0u) {
            return owner;
        }
        std::uint32_t previous_r1 = 0u;
        if (!read_previous_r1(frame, previous_r1) ||
            previous_r1 == 0u || previous_r1 == frame) {
            return 0u;
        }
        frame = previous_r1;
    }
    return 0u;
}

[[nodiscard]] constexpr GuestSleepIdentityDisposition
classify_guest_sleep_identity(
    const GuestSleepIdentityEvidence& evidence) noexcept {
    constexpr std::uint16_t kRunningThreadState = 2u;
    if (evidence.current_context == 0u) {
        return GuestSleepIdentityDisposition::RejectNullCurrentContext;
    }
    if (!evidence.current_context_is_mapped) {
        return GuestSleepIdentityDisposition::RejectUnmappedCurrentContext;
    }
    if (evidence.physical_context !=
        (evidence.current_context & 0x3FFFFFFFu)) {
        return GuestSleepIdentityDisposition::RejectPhysicalContextAlias;
    }
    if (evidence.current_thread == 0u) {
        return GuestSleepIdentityDisposition::RejectNullCurrentThread;
    }
    if (!evidence.current_thread_is_mapped) {
        return GuestSleepIdentityDisposition::RejectUnmappedCurrentThread;
    }
    if (!evidence.current_thread_is_active) {
        return GuestSleepIdentityDisposition::RejectInactiveCurrentThread;
    }
    if (evidence.current_thread_state != kRunningThreadState) {
        return GuestSleepIdentityDisposition::RejectNonRunningCurrentThread;
    }
    if (evidence.current_context != evidence.current_thread &&
        evidence.context_owner_thread != evidence.current_thread) {
        return GuestSleepIdentityDisposition::RejectContextOwnerMismatch;
    }
    if (evidence.r1_owner_thread != evidence.current_thread) {
        return GuestSleepIdentityDisposition::RejectStackOwnerMismatch;
    }
    return GuestSleepIdentityDisposition::Valid;
}

[[nodiscard]] constexpr GuestSleepQueueDisposition classify_guest_sleep_queue(
    std::uint32_t queue,
    bool queue_header_is_mapped) noexcept {
    if (queue == 0u) {
        return GuestSleepQueueDisposition::RejectNullQueue;
    }
    if ((queue & (alignof(std::uint32_t) - 1u)) != 0u) {
        return GuestSleepQueueDisposition::RejectMisalignedQueue;
    }
    if (!queue_header_is_mapped) {
        return GuestSleepQueueDisposition::RejectUnmappedQueue;
    }
    return GuestSleepQueueDisposition::RunTranslatedScheduler;
}

struct FlatContinuationStop {
    FlatContinuationStopReason reason{};
    std::uint32_t initial_pc{};
    std::uint32_t previous_pc{};
    std::uint32_t next_pc{};
    std::uint32_t returned_lr{};
    std::uint64_t completed_hops{};
};

// Runs one flat guest timeline. Each translated continuation must be found by
// its exact alias. A continuation's normal native return means guest `blr`, so
// the next hop comes only from the LR it produced. Architectural transfers
// thrown by invoke() deliberately propagate to the top-level owner.
template <typename Normalize, typename LookupExact, typename Invoke,
          typename IsStackTopSentinel>
[[nodiscard]] FlatContinuationStop run_flat_continuation_chain(
    std::uint32_t transfer_pc,
    Normalize&& normalize,
    LookupExact&& lookup_exact,
    Invoke&& invoke,
    IsStackTopSentinel&& is_stack_top_sentinel) {
    const std::uint32_t initial_pc = normalize(transfer_pc);
    std::uint32_t previous_pc = 0u;
    std::uint32_t next_pc = initial_pc;
    std::uint32_t returned_lr = 0u;
    std::uint64_t completed_hops = 0u;

    for (;;) {
        auto continuation = lookup_exact(next_pc);
        if (!continuation) {
            const bool at_stack_top =
                completed_hops != 0u &&
                is_stack_top_sentinel(returned_lr);
            return FlatContinuationStop{
                at_stack_top
                    ? FlatContinuationStopReason::StackTopSentinel
                    : FlatContinuationStopReason::MissingExactContinuation,
                initial_pc,
                previous_pc,
                next_pc,
                returned_lr,
                completed_hops};
        }

        previous_pc = next_pc;
        returned_lr = static_cast<std::uint32_t>(
            invoke(previous_pc, continuation));
        ++completed_hops;
        next_pc = normalize(returned_lr);
    }
}

}  // namespace galaxy::scheduler
