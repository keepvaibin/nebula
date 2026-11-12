#include "Game/System/LumaGuard.hpp"

#include "Game/System/LumaLoader.hpp"
#include "Game/System/LumaModLog.hpp"
#include <luma_guard.h>
#include <nebula_game_api.h>
#include <nebula_format.h>
#include <nebula_module.h>
#include <stddef.h>
#include <stdint.h>

// The call and its resume (luma_guard.h), per calling convention. luma_guard_call stores the registers a call keeps,
// the stack pointer as it is after the return and the return address in frame->regs, calls frame->fn with
// frame->args[0..3], stores its integer result in frame->result and answers 0. The host's handler points a faulting
// thread at luma_guard_resume with the frame as its first argument: it loads the registers back and jumps to that
// return address with 1 as the answer, so luma_guard_call returns a second time. The stack below is left as it is: the
// host resumes only when the frames between hold nothing of the game or the host (contract guard 2), and nothing of
// the kernel is there but luma_guard_call's own few bytes.
extern "C" {
__attribute__((returns_twice)) int luma_guard_call(LumaGuardFrame* frame);
void luma_guard_resume(LumaGuardFrame* frame);
extern const char luma_guard_call_return[];
NEBULA_GAME_API int32_t luma_guard_cost(const char* mod, uint64_t* avg_ns, uint64_t* worst_ns);
}

static_assert(offsetof(LumaGuardFrame, fn) == 256 && offsetof(LumaGuardFrame, args) == 264 && offsetof(LumaGuardFrame, result) == 296,
              "luma_guard_call reads the frame at these offsets");

#if defined(__x86_64__) && defined(_WIN32)
// win-x64. The function pushes rbx (the frame across the call) and the 32 bytes of shadow space; its unwind data says
// so, so stack walks pass through it.
__asm__(".text\n"
        ".globl luma_guard_call\n"
        ".def luma_guard_call; .scl 2; .type 32; .endef\n"
        ".p2align 4\n"
        "luma_guard_call:\n"
        ".seh_proc luma_guard_call\n"
        "    movq %rbx, 0(%rcx)\n"
        "    movq %rbp, 8(%rcx)\n"
        "    movq %rdi, 16(%rcx)\n"
        "    movq %rsi, 24(%rcx)\n"
        "    movq %r12, 32(%rcx)\n"
        "    movq %r13, 40(%rcx)\n"
        "    movq %r14, 48(%rcx)\n"
        "    movq %r15, 56(%rcx)\n"
        "    leaq 8(%rsp), %rdx\n"
        "    movq %rdx, 64(%rcx)\n"
        "    movq (%rsp), %rdx\n"
        "    movq %rdx, 72(%rcx)\n"
        "    stmxcsr 80(%rcx)\n"
        "    fnstcw 84(%rcx)\n"
        "    movdqu %xmm6, 96(%rcx)\n"
        "    movdqu %xmm7, 112(%rcx)\n"
        "    movdqu %xmm8, 128(%rcx)\n"
        "    movdqu %xmm9, 144(%rcx)\n"
        "    movdqu %xmm10, 160(%rcx)\n"
        "    movdqu %xmm11, 176(%rcx)\n"
        "    movdqu %xmm12, 192(%rcx)\n"
        "    movdqu %xmm13, 208(%rcx)\n"
        "    movdqu %xmm14, 224(%rcx)\n"
        "    movdqu %xmm15, 240(%rcx)\n"
        "    pushq %rbx\n"
        ".seh_pushreg %rbx\n"
        "    subq $32, %rsp\n"
        ".seh_stackalloc 32\n"
        ".seh_endprologue\n"
        "    movq %rcx, %rbx\n"
        "    movq 264(%rbx), %rcx\n"
        "    movq 272(%rbx), %rdx\n"
        "    movq 280(%rbx), %r8\n"
        "    movq 288(%rbx), %r9\n"
        "    callq *256(%rbx)\n"
        ".globl luma_guard_call_return\n"
        "luma_guard_call_return:\n"
        "    movq %rax, 296(%rbx)\n"
        "    addq $32, %rsp\n"
        "    popq %rbx\n"
        "    xorl %eax, %eax\n"
        "    ret\n"
        ".seh_endproc\n"
        ".globl luma_guard_resume\n"
        ".def luma_guard_resume; .scl 2; .type 32; .endef\n"
        ".p2align 4\n"
        "luma_guard_resume:\n"
        "    movq 0(%rcx), %rbx\n"
        "    movq 8(%rcx), %rbp\n"
        "    movq 16(%rcx), %rdi\n"
        "    movq 24(%rcx), %rsi\n"
        "    movq 32(%rcx), %r12\n"
        "    movq 40(%rcx), %r13\n"
        "    movq 48(%rcx), %r14\n"
        "    movq 56(%rcx), %r15\n"
        "    movdqu 96(%rcx), %xmm6\n"
        "    movdqu 112(%rcx), %xmm7\n"
        "    movdqu 128(%rcx), %xmm8\n"
        "    movdqu 144(%rcx), %xmm9\n"
        "    movdqu 160(%rcx), %xmm10\n"
        "    movdqu 176(%rcx), %xmm11\n"
        "    movdqu 192(%rcx), %xmm12\n"
        "    movdqu 208(%rcx), %xmm13\n"
        "    movdqu 224(%rcx), %xmm14\n"
        "    movdqu 240(%rcx), %xmm15\n"
        "    ldmxcsr 80(%rcx)\n"
        "    fldcw 84(%rcx)\n"
        "    movq 64(%rcx), %rsp\n"
        "    movl $1, %eax\n"
        "    jmpq *72(%rcx)\n");
#elif defined(__x86_64__)
// Each symbol is the module's own (not exported): Mach-O .private_extern, ELF .hidden.
#if defined(__APPLE__)
#define LUMA_GUARD_SYM(name) "_" #name
#define LUMA_GUARD_HIDE(name) ".private_extern _" #name "\n"
#define LUMA_GUARD_TYPE(name) LUMA_GUARD_HIDE(name)
#else
#define LUMA_GUARD_SYM(name) #name
#define LUMA_GUARD_HIDE(name) ".hidden " #name "\n"
#define LUMA_GUARD_TYPE(name) ".type " #name ", @function\n" LUMA_GUARD_HIDE(name)
#endif
// sysv-x64: pushes rbx (the frame across the call), which also aligns the call; its CFI says so.
__asm__(".text\n"
        ".globl " LUMA_GUARD_SYM(luma_guard_call) "\n" LUMA_GUARD_TYPE(luma_guard_call)
        ".p2align 4\n"
        LUMA_GUARD_SYM(luma_guard_call) ":\n"
        ".cfi_startproc\n"
        "    movq %rbx, 0(%rdi)\n"
        "    movq %rbp, 8(%rdi)\n"
        "    movq %r12, 16(%rdi)\n"
        "    movq %r13, 24(%rdi)\n"
        "    movq %r14, 32(%rdi)\n"
        "    movq %r15, 40(%rdi)\n"
        "    leaq 8(%rsp), %rdx\n"
        "    movq %rdx, 48(%rdi)\n"
        "    movq (%rsp), %rdx\n"
        "    movq %rdx, 56(%rdi)\n"
        "    stmxcsr 64(%rdi)\n"
        "    fnstcw 68(%rdi)\n"
        "    pushq %rbx\n"
        ".cfi_adjust_cfa_offset 8\n"
        ".cfi_rel_offset %rbx, 0\n"
        "    movq %rdi, %rbx\n"
        "    movq 264(%rbx), %rdi\n"
        "    movq 272(%rbx), %rsi\n"
        "    movq 280(%rbx), %rdx\n"
        "    movq 288(%rbx), %rcx\n"
        "    callq *256(%rbx)\n"
        ".globl " LUMA_GUARD_SYM(luma_guard_call_return) "\n" LUMA_GUARD_HIDE(luma_guard_call_return)
        LUMA_GUARD_SYM(luma_guard_call_return) ":\n"
        "    movq %rax, 296(%rbx)\n"
        "    popq %rbx\n"
        ".cfi_adjust_cfa_offset -8\n"
        ".cfi_restore %rbx\n"
        "    xorl %eax, %eax\n"
        "    ret\n"
        ".cfi_endproc\n"
        ".globl " LUMA_GUARD_SYM(luma_guard_resume) "\n" LUMA_GUARD_TYPE(luma_guard_resume)
        ".p2align 4\n"
        LUMA_GUARD_SYM(luma_guard_resume) ":\n"
        "    movq 0(%rdi), %rbx\n"
        "    movq 8(%rdi), %rbp\n"
        "    movq 16(%rdi), %r12\n"
        "    movq 24(%rdi), %r13\n"
        "    movq 32(%rdi), %r14\n"
        "    movq 40(%rdi), %r15\n"
        "    ldmxcsr 64(%rdi)\n"
        "    fldcw 68(%rdi)\n"
        "    movq 48(%rdi), %rsp\n"
        "    movl $1, %eax\n"
        "    jmpq *56(%rdi)\n");
#elif defined(__aarch64__) && !defined(_WIN32)
// Each symbol is the module's own (not exported): Mach-O .private_extern, ELF .hidden.
#if defined(__APPLE__)
#define LUMA_GUARD_SYM(name) "_" #name
#define LUMA_GUARD_HIDE(name) ".private_extern _" #name "\n"
#define LUMA_GUARD_TYPE(name) LUMA_GUARD_HIDE(name)
#else
#define LUMA_GUARD_SYM(name) #name
#define LUMA_GUARD_HIDE(name) ".hidden " #name "\n"
#define LUMA_GUARD_TYPE(name) ".type " #name ", %function\n" LUMA_GUARD_HIDE(name)
#endif
// aarch64: a 32-byte frame (fp, lr, x19 holding the frame across the call); its CFI says so. The resume returns with
// `ret` through x30, which branch target checks do not apply to.
__asm__(".text\n"
        ".globl " LUMA_GUARD_SYM(luma_guard_call) "\n" LUMA_GUARD_TYPE(luma_guard_call)
        ".p2align 4\n"
        LUMA_GUARD_SYM(luma_guard_call) ":\n"
        ".cfi_startproc\n"
        "    stp x19, x20, [x0, #0]\n"
        "    stp x21, x22, [x0, #16]\n"
        "    stp x23, x24, [x0, #32]\n"
        "    stp x25, x26, [x0, #48]\n"
        "    stp x27, x28, [x0, #64]\n"
        "    stp x29, x30, [x0, #80]\n"
        "    mov x16, sp\n"
        "    str x16, [x0, #96]\n"
        "    stp d8, d9, [x0, #104]\n"
        "    stp d10, d11, [x0, #120]\n"
        "    stp d12, d13, [x0, #136]\n"
        "    stp d14, d15, [x0, #152]\n"
        "    mrs x16, fpcr\n"
        "    str x16, [x0, #168]\n"
        "    stp x29, x30, [sp, #-32]!\n"
        ".cfi_def_cfa_offset 32\n"
        ".cfi_offset x29, -32\n"
        ".cfi_offset x30, -24\n"
        "    mov x29, sp\n"
        "    str x19, [sp, #16]\n"
        ".cfi_offset x19, -16\n"
        "    mov x19, x0\n"
        "    ldp x0, x1, [x19, #264]\n"
        "    ldp x2, x3, [x19, #280]\n"
        "    ldr x16, [x19, #256]\n"
        "    blr x16\n"
        ".globl " LUMA_GUARD_SYM(luma_guard_call_return) "\n" LUMA_GUARD_HIDE(luma_guard_call_return)
        LUMA_GUARD_SYM(luma_guard_call_return) ":\n"
        "    str x0, [x19, #296]\n"
        "    ldr x19, [sp, #16]\n"
        "    ldp x29, x30, [sp], #32\n"
        ".cfi_def_cfa_offset 0\n"
        ".cfi_restore x19\n"
        ".cfi_restore x29\n"
        ".cfi_restore x30\n"
        "    mov w0, #0\n"
        "    ret\n"
        ".cfi_endproc\n"
        ".globl " LUMA_GUARD_SYM(luma_guard_resume) "\n" LUMA_GUARD_TYPE(luma_guard_resume)
        ".p2align 4\n"
        LUMA_GUARD_SYM(luma_guard_resume) ":\n"
        "    ldp x19, x20, [x0, #0]\n"
        "    ldp x21, x22, [x0, #16]\n"
        "    ldp x23, x24, [x0, #32]\n"
        "    ldp x25, x26, [x0, #48]\n"
        "    ldp x27, x28, [x0, #64]\n"
        "    ldp x29, x30, [x0, #80]\n"
        "    ldp d8, d9, [x0, #104]\n"
        "    ldp d10, d11, [x0, #120]\n"
        "    ldp d12, d13, [x0, #136]\n"
        "    ldp d14, d15, [x0, #152]\n"
        "    ldr x16, [x0, #168]\n"
        "    msr fpcr, x16\n"
        "    ldr x16, [x0, #96]\n"
        "    mov sp, x16\n"
        "    mov w0, #1\n"
        "    ret\n");
#else
#error "LumaCore's guard has no call for this processor or system (luma_guard.h; Windows on ARM64: contract guard 5)"
#endif

namespace luma {
    namespace guard {
        namespace {
            // The host's half; null until luma_guard_install (a host without it: frames are kept, nothing resumes).
            const LumaGuardHost* sHost = nullptr;
            bool sMeasured = false;  // measureForHarness ran (game thread)

            // The empty function measureForHarness calls both ways.
            __attribute__((noinline)) u64 nothing(uintptr_t a0, uintptr_t a1) {
                __asm__ volatile("" ::: "memory");
                return a0 + a1;
            }
            // This thread's stack of frames: constant-initialised, so no first-use code (no _Init_thread).
            thread_local LumaGuardThread tThread = {nullptr, 0u, 0u};
            // The ticks the boundaries nested in this thread's innermost one took (its own time is its total less them).
            thread_local u64 tNestedTicks = 0;

            // ---- Code time (luma_guard_cost) ----
            // The cheapest clock there is, never a system call (a hook can run thousands of times a frame): the
            // time-stamp counter on x64 (invariant on every CPU the game runs on), the virtual counter on arm64.
            inline u64 ticks() {
#if defined(__x86_64__)
                return __builtin_ia32_rdtsc();
#else
                u64 value;
                __asm__ volatile("mrs %0, cntvct_el0" : "=r"(value));
                return value;
#endif
            }

            // ns per tick, as the bits of a double: 0 until scaled. arm64: from the counter's frequency at install.
            // x64: against the host's clock, once, over the first quarter second after install (frameEnd).
            u64 sNsPerTickBits = 0;
            u64 sScaleTicks0 = 0;
            u64 sScaleNs0 = 0;
            bool sScaleFailed = false;

            double nsPerTick() {
                const u64 bits = __atomic_load_n(&sNsPerTickBits, __ATOMIC_ACQUIRE);
                double value;
                __builtin_memcpy(&value, &bits, sizeof(value));
                return value;
            }

            void setNsPerTick(double value) {
                u64 bits;
                __builtin_memcpy(&bits, &value, sizeof(bits));
                __atomic_store_n(&sNsPerTickBits, bits, __ATOMIC_RELEASE);
            }

            const u32 cCostFrames = 120;
            const u32 cCostSlots = 256;  // mods loaded in a session (a reload of the same id reuses its slot)
            struct CostSlot {
                u32 seq;           // even: steady; odd: being written (game thread only writes)
                u32 live;          // rolled each frame (0 once its mod ended)
                LumaMod* pOwner;   // the mod loaded last with this id (only its end stops the rolling)
                u32 told;          // its fact guard.time written (harness runs)
                char id[72];       // the mod's id (at most 64 bytes)
                u64 frameTicks;    // this frame's, added to from any thread
                u64 ring[cCostFrames];
                u32 at;
                u32 filled;
                u64 sumTicks;      // over the ring
                u64 worstTicks;
            };
            // Zero, so no page of it is touched until a mod loads.
            CostSlot sCostSlots[cCostSlots];
            u32 sCostUsed = 0;    // slots ever handed out (published with release; readers scan below it)
            bool sCostFull = false;

            CostSlot* costSlotOf(LumaMod* pMod) {
                const u32 slot = *modCostSlot(pMod);
                return slot != 0 ? &sCostSlots[slot - 1] : nullptr;
            }

            void charge(LumaMod* pMod, u64 ownTicks) {
                if (CostSlot* const pSlot = costSlotOf(pMod)) {
                    __atomic_fetch_add(&pSlot->frameTicks, ownTicks, __ATOMIC_RELAXED);
                }
            }

            const char* const cBoundaryRows[LUMA_GUARD_BOUNDARY_COUNT] = {
                "R:Guard.Boundary.Before", "R:Guard.Boundary.After", "R:Guard.Boundary.Wrap",  "R:Guard.Boundary.Tell",
                "R:Guard.Boundary.Start",  "R:Guard.Boundary.Stop",  "R:Guard.Boundary.Event", "R:Guard.Boundary.Class"};
            const char* const cBoundaryWords[LUMA_GUARD_BOUNDARY_COUNT] = {"before hook", "after hook", "wrap hook", "hook",
                                                                           "start",       "stop",       "service event", "class"};

            const char* faultRow(u32 fault) {
                switch (fault) {
                case LUMA_GUARD_ACCESS: return "R:Guard.Fault.Access";
                case LUMA_GUARD_ILLEGAL: return "R:Guard.Fault.Illegal";
                default: return "R:Guard.Fault.Divide";
                }
            }

            const char* faultWords(u32 fault) {
                switch (fault) {
                case LUMA_GUARD_ACCESS: return "a memory access it may not make";
                case LUMA_GUARD_ILLEGAL: return "an instruction the processor refuses";
                default: return "a division by zero";
                }
            }

            bool same(const char* pA, const char* pB) {
                while (*pA != 0 && *pA == *pB) {
                    pA++;
                    pB++;
                }
                return *pA == *pB;
            }

            // "<file>+0x<offset>" (the place in the mod's library), or "0x<address>" for an empty file; hex by hand.
            void placeOf(char* pDst, u32 size, const char* pFile, uintptr_t offset) {
                u32 at = 0;
                for (const char* p = pFile; *p != 0 && at + 1 < size; p++) {
                    pDst[at++] = *p;
                }
                const char* const cHex = "0123456789abcdef";
                char digits[20];
                u32 count = 0;
                do {
                    digits[count++] = cHex[offset & 15u];
                    offset >>= 4;
                } while (offset != 0 && count < sizeof(digits));
                for (const char* p = *pFile != 0 ? "+0x" : "0x"; *p != 0 && at + 1 < size; p++) {
                    pDst[at++] = *p;
                }
                while (count > 0 && at + 1 < size) {
                    pDst[at++] = digits[--count];
                }
                pDst[at] = 0;
            }

            // After a resume, on the faulting thread, with the frame already off the stack: the mod off, said, taken out.
            void caught(LumaMod* pMod, const LumaGuardFrame& rFrame) {
                __atomic_store_n(modFaulted(pMod), 1u, __ATOMIC_RELEASE);
                const u32 kind = rFrame.boundary < LUMA_GUARD_BOUNDARY_COUNT ? rFrame.boundary : LUMA_GUARD_START;
                uintptr_t base = 0, begin = 0, end = 0;
                modCode(pMod, &base, &begin, &end);
                char place[160];
                placeOf(place, sizeof(place), modFile(pMod), rFrame.pc - base);
                char address[24];
                placeOf(address, sizeof(address), "", rFrame.address);
                nebula_log("[mod %s] GUARD: its code failed (%s: %s): %s at %s, address %s. It is off until the next start; "
                           "what it was changing may be half done.\n",
                           rFrame.mod, cBoundaryWords[kind], rFrame.where, faultWords(rFrame.fault), place, address);
                LumaModLogEntry entry = {};
                entry.size = sizeof(entry);
                entry.source = LUMA_MODLOG_GUARD;
                entry.level = LUMA_MODLOG_ERROR;
                entry.code = rFrame.fault;
                entry.mod = rFrame.mod;
                entry.file = rFrame.where;
                entry.names[0] = "boundary";
                entry.values[0] = cBoundaryRows[kind];
                entry.names[1] = "where";
                entry.values[1] = rFrame.where;
                entry.names[2] = "fault";
                entry.values[2] = faultRow(rFrame.fault);
                entry.names[3] = "address";
                entry.values[3] = place;
                entry.count = 4;
                luma::modlog::add(entry, "Guard.Caught");
                // Its hooks go at the next frame's start, with the mods that use it; its stop and finish do not run. A
                // fault in begin or start: the loader refuses it there and then; in stop or finish: it is going already.
                if (!(kind == LUMA_GUARD_START || (kind == LUMA_GUARD_STOP && (same(rFrame.where, "stop") || same(rFrame.where, "finish"))))) {
                    requestTakeOut(rFrame.mod);
                }
            }
        }  // namespace

        bool faulted(const LumaMod* pMod) {
            return __atomic_load_n(modFaulted(const_cast<LumaMod*>(pMod)), __ATOMIC_ACQUIRE) != 0;
        }

        void codeRange(void* pLibrary, uintptr_t* pBase, uintptr_t* pBegin, uintptr_t* pEnd) {
            *pBase = 0;
            *pBegin = 0;
            *pEnd = 0;
            if (sHost != nullptr && pLibrary != nullptr && sHost->code_range(pLibrary, pBase, pBegin, pEnd) != 0) {
                *pBase = 0;
                *pBegin = 0;
                *pEnd = 0;
            }
        }

        bool run(LumaMod* pMod, u32 kind, const char* pWhere, const void* fn, uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3,
                 u64* pResult) {
            if (pResult != nullptr) {
                *pResult = 0;
            }
            if (faulted(pMod)) {
                return false;
            }
            LumaGuardThread* const pThread = &tThread;
            if (pThread->entered == 0u) {
                pThread->entered = 1u;
                if (sHost != nullptr && sHost->thread_enter(pThread) != 0) {
                    nebula_log("LumaCore: the guard is not on for a thread running mods' code; a fault there ends the game "
                               "with its report\n");
                }
            }
            // Its own time: the total less what the boundaries nested in it took (they charge their own mods).
            const u64 outerNested = tNestedTicks;
            tNestedTicks = 0;
            LumaGuardFrame frame;
            frame.fn = const_cast<void*>(fn);
            frame.args[0] = a0;
            frame.args[1] = a1;
            frame.args[2] = a2;
            frame.args[3] = a3;
            frame.result = 0;
            frame.outer = pThread->top;
            frame.mod = modId(pMod);
            frame.where = pWhere != nullptr ? pWhere : "";
            frame.boundary = kind;
            frame.fault = LUMA_GUARD_NONE;
            uintptr_t base = 0;
            modCode(pMod, &base, &frame.code_begin, &frame.code_end);
            frame.pc = 0;
            frame.address = 0;
            frame.resume = luma_guard_resume;
            frame.call_return = reinterpret_cast<uintptr_t>(luma_guard_call_return);
            // The frame is complete before the host's handler, on this thread, can see it.
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
            pThread->top = &frame;
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
            const u64 start = ticks();
            const bool returned = luma_guard_call(&frame) == 0;
            const u64 total = ticks() - start;
            charge(pMod, total > tNestedTicks ? total - tNestedTicks : 0);
            tNestedTicks = outerNested + total;
            if (returned) {
                pThread->top = frame.outer;
                if (pResult != nullptr) {
                    *pResult = frame.result;
                }
                return true;
            }
            // Resumed: the host wrote the fault into the frame and set `handling`.
            pThread->top = frame.outer;
            caught(pMod, frame);
            pThread->handling = 0u;
            return false;
        }

        void measureForHarness() {
            // Once, in a harness run only (the facts writer is set): the boundary's cost against a plain call of the same
            // empty function, on this thread, through the same frame fill, push, luma_guard_call and pop as run().
            static_assert(sizeof(LumaGuardFrame) <= 512, "the frame stays small");
            if (sMeasured || !luma::modlog::facts()) {
                return;
            }
            sMeasured = true;
            const NebulaHostServices* const pHost = nebula_host();
            if (pHost == nullptr || pHost->clock_ns == nullptr) {
                return;
            }
            const u32 cCount = 100000;
            u64 (*volatile plain)(uintptr_t, uintptr_t) = nothing;
            u64 sink = 0;
            const u64 t0 = pHost->clock_ns(pHost->user);
            for (u32 i = 0; i < cCount; i++) {
                sink += plain(i, 0);
            }
            const u64 t1 = pHost->clock_ns(pHost->user);
            LumaGuardThread* const pThread = &tThread;
            u64 measured = 0;  // stands for a mod's slot: the charge is an add to memory, as in run()
            for (u32 i = 0; i < cCount; i++) {
                const u64 outerNested = tNestedTicks;
                tNestedTicks = 0;
                LumaGuardFrame frame;
                frame.fn = reinterpret_cast<void*>(plain);
                frame.args[0] = i;
                frame.args[1] = 0;
                frame.args[2] = 0;
                frame.args[3] = 0;
                frame.result = 0;
                frame.outer = pThread->top;
                frame.mod = "guard";
                frame.where = "measure";
                frame.boundary = LUMA_GUARD_EVENT;
                frame.fault = LUMA_GUARD_NONE;
                frame.code_begin = 0;
                frame.code_end = 0;
                frame.pc = 0;
                frame.address = 0;
                frame.resume = luma_guard_resume;
                frame.call_return = reinterpret_cast<uintptr_t>(luma_guard_call_return);
                __atomic_signal_fence(__ATOMIC_SEQ_CST);
                pThread->top = &frame;
                __atomic_signal_fence(__ATOMIC_SEQ_CST);
                const u64 start = ticks();
                if (luma_guard_call(&frame) == 0) {
                    sink += frame.result;
                }
                const u64 total = ticks() - start;
                __atomic_fetch_add(&measured, total > tNestedTicks ? total - tNestedTicks : 0, __ATOMIC_RELAXED);
                tNestedTicks = outerNested + total;
                pThread->top = frame.outer;
            }
            const u64 t2 = pHost->clock_ns(pHost->user);
            // The clock's pair of reads alone, as each boundary makes it.
            u64 pairs = 0;
            for (u32 i = 0; i < cCount; i++) {
                const u64 a = ticks();
                pairs += ticks() - a;
            }
            const u64 t3 = pHost->clock_ns(pHost->user);
            sink += measured + pairs;
            char json[200];
            nebula_snprintf(json, sizeof(json),
                            "{\"count\": %u, \"plain_ns\": %llu, \"guarded_ns\": %llu, \"clock_pairs_ns\": %llu, \"sink\": %llu}",
                            cCount, static_cast<unsigned long long>(t1 - t0), static_cast<unsigned long long>(t2 - t1),
                            static_cast<unsigned long long>(t3 - t2), static_cast<unsigned long long>(sink));
            luma::modlog::fact("guard.cost", json);
            nebula_log("LumaCore: the guard's boundary costs %llu ns per call more than a plain call, its clock's pair of reads "
                       "%llu ns of that (%u calls: %llu ns against %llu ns; %u pairs: %llu ns)\n",
                       static_cast<unsigned long long>(t2 - t1 > t1 - t0 ? (t2 - t1 - (t1 - t0)) / cCount : 0),
                       static_cast<unsigned long long>((t3 - t2) / cCount), cCount, static_cast<unsigned long long>(t2 - t1),
                       static_cast<unsigned long long>(t1 - t0), cCount, static_cast<unsigned long long>(t3 - t2));
        }

        bool install(const LumaGuardHost* pHost) {
            if (pHost == nullptr || pHost->size < sizeof(LumaGuardHost)) {
                return false;
            }
            sHost = pHost;
#if defined(__aarch64__)
            u64 frequency;
            __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
            if (frequency != 0) {
                setNsPerTick(1e9 / static_cast<double>(frequency));
            }
#endif
            return true;
        }

        void modBegin(LumaMod* pMod) {
            const char* const pId = modId(pMod);
            u32 found = sCostUsed;
            for (u32 i = 0; i < sCostUsed; i++) {
                if (same(sCostSlots[i].id, pId)) {
                    found = i;
                    break;
                }
            }
            if (found == cCostSlots) {
                if (!sCostFull) {
                    sCostFull = true;
                    nebula_log("LumaCore: %u mods' code times are kept this session; later ones are not measured\n", cCostSlots);
                }
                return;
            }
            CostSlot& rSlot = sCostSlots[found];
            __atomic_store_n(&rSlot.seq, rSlot.seq + 1u, __ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_RELEASE);
            u32 at = 0;
            for (; pId[at] != 0 && at + 1 < sizeof(rSlot.id); at++) {
                rSlot.id[at] = pId[at];
            }
            rSlot.id[at] = 0;
            __atomic_store_n(&rSlot.frameTicks, 0, __ATOMIC_RELAXED);
            for (u32 i = 0; i < cCostFrames; i++) {
                rSlot.ring[i] = 0;
            }
            rSlot.at = 0;
            rSlot.filled = 0;
            rSlot.sumTicks = 0;
            rSlot.worstTicks = 0;
            rSlot.live = 1;
            rSlot.pOwner = pMod;
            rSlot.told = 0;
            __atomic_store_n(&rSlot.seq, rSlot.seq + 1u, __ATOMIC_RELEASE);
            if (found == sCostUsed) {
                __atomic_store_n(&sCostUsed, sCostUsed + 1u, __ATOMIC_RELEASE);
            }
            *modCostSlot(pMod) = found + 1;
        }

        void modEnd(LumaMod* pMod) {
            CostSlot* const pSlot = costSlotOf(pMod);
            if (pSlot != nullptr && pSlot->pOwner == pMod) {
                pSlot->live = 0;
                pSlot->pOwner = nullptr;
            }
        }

        void frameEnd() {
            const u32 used = sCostUsed;
            if (used == 0) {
                return;
            }
            // x64: the counter's scale against the host's clock, once, over the first quarter second with a mod.
            if (nsPerTick() <= 0.0 && !sScaleFailed) {
                const NebulaHostServices* const pServices = nebula_host();
                if (pServices == nullptr || pServices->clock_ns == nullptr) {
                    sScaleFailed = true;
                    nebula_log("LumaCore: no host clock, so the mods' code times are not measured\n");
                } else {
                    const u64 ns = pServices->clock_ns(pServices->user);
                    const u64 now = ticks();
                    if (sScaleTicks0 == 0) {
                        sScaleNs0 = ns;
                        sScaleTicks0 = now;
                    } else if (ns - sScaleNs0 >= 250000000ull && now > sScaleTicks0) {
                        setNsPerTick(static_cast<double>(ns - sScaleNs0) / static_cast<double>(now - sScaleTicks0));
                    }
                }
            }
            for (u32 s = 0; s < used; s++) {
                CostSlot& rSlot = sCostSlots[s];
                if (rSlot.live == 0) {
                    continue;
                }
                const u64 frameTicks = __atomic_exchange_n(&rSlot.frameTicks, 0, __ATOMIC_RELAXED);
                __atomic_store_n(&rSlot.seq, rSlot.seq + 1u, __ATOMIC_RELAXED);
                __atomic_thread_fence(__ATOMIC_RELEASE);
                rSlot.sumTicks = rSlot.sumTicks - rSlot.ring[rSlot.at] + frameTicks;
                rSlot.ring[rSlot.at] = frameTicks;
                rSlot.at = rSlot.at + 1 == cCostFrames ? 0 : rSlot.at + 1;
                if (rSlot.filled < cCostFrames) {
                    rSlot.filled++;
                }
                u64 worst = 0;
                for (u32 i = 0; i < cCostFrames; i++) {
                    worst = rSlot.ring[i] > worst ? rSlot.ring[i] : worst;
                }
                rSlot.worstTicks = worst;
                __atomic_store_n(&rSlot.seq, rSlot.seq + 1u, __ATOMIC_RELEASE);
                // In a harness run, once per load: the first full ring as luma_guard_cost gives it (contract guard 4).
                if (rSlot.filled == cCostFrames && rSlot.told == 0 && luma::modlog::facts()) {
                    rSlot.told = 1;
                    u64 avgNs = 0;
                    u64 worstNs = 0;
                    const int32_t found = luma_guard_cost(rSlot.id, &avgNs, &worstNs);
                    char json[200];
                    nebula_snprintf(json, sizeof(json), "{\"mod\": \"%s\", \"found\": %d, \"avg_ns\": %llu, \"worst_ns\": %llu}", rSlot.id,
                                    static_cast<int>(found), static_cast<unsigned long long>(avgNs),
                                    static_cast<unsigned long long>(worstNs));
                    luma::modlog::fact("guard.time", json);
                }
            }
        }
    }  // namespace guard
}  // namespace luma

extern "C" {
NEBULA_GAME_API int32_t luma_guard_install(const LumaGuardHost* host) {
    return luma::guard::install(host) ? 1 : 0;
}

// Any thread: a seqlock read of the mod's slot (written on the game thread only).
NEBULA_GAME_API int32_t luma_guard_cost(const char* mod, uint64_t* avg_ns, uint64_t* worst_ns) {
    using namespace luma::guard;
    if (avg_ns != nullptr) {
        *avg_ns = 0;
    }
    if (worst_ns != nullptr) {
        *worst_ns = 0;
    }
    const double scale = nsPerTick();
    if (mod == nullptr || scale <= 0.0) {
        return -1;
    }
    const u32 used = __atomic_load_n(&sCostUsed, __ATOMIC_ACQUIRE);
    for (u32 s = 0; s < used; s++) {
        const CostSlot& rSlot = sCostSlots[s];
        for (;;) {
            const u32 before = __atomic_load_n(&rSlot.seq, __ATOMIC_ACQUIRE);
            if ((before & 1u) != 0) {
                continue;
            }
            char id[sizeof(rSlot.id)];
            for (u32 i = 0; i < sizeof(id); i++) {
                id[i] = __atomic_load_n(&rSlot.id[i], __ATOMIC_RELAXED);
            }
            const u64 sum = __atomic_load_n(&rSlot.sumTicks, __ATOMIC_RELAXED);
            const u64 worst = __atomic_load_n(&rSlot.worstTicks, __ATOMIC_RELAXED);
            const u32 filled = __atomic_load_n(&rSlot.filled, __ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            if (__atomic_load_n(&rSlot.seq, __ATOMIC_RELAXED) != before) {
                continue;
            }
            id[sizeof(id) - 1] = 0;
            if (!same(id, mod)) {
                break;
            }
            if (avg_ns != nullptr) {
                *avg_ns = filled != 0 ? static_cast<uint64_t>(static_cast<double>(sum) / filled * scale) : 0;
            }
            if (worst_ns != nullptr) {
                *worst_ns = static_cast<uint64_t>(static_cast<double>(worst) * scale);
            }
            return 0;
        }
    }
    return -1;
}
}
