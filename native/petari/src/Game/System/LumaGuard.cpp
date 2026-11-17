#include "Game/System/LumaGuard.hpp"

#include "Game/System/LumaLoader.hpp"
#include "Game/System/LumaModLog.hpp"
#include <luma_guard.h>
#include <luma_guard_walk.h>
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
// host resumes only when the frames between hold nothing of the game or the host, and nothing of
// the kernel is there but luma_guard_call's own few bytes.
extern "C" {
__attribute__((returns_twice)) int luma_guard_call(LumaGuardFrame* frame);
void luma_guard_resume(LumaGuardFrame* frame);
extern const char luma_guard_call_return[];
NEBULA_GAME_API int32_t luma_guard_cost(const char* mod, uint64_t* avg_ns, uint64_t* worst_ns);
NEBULA_GAME_API int32_t luma_guard_blame(const LumaGuardThread* thread, const LumaGuardFrame* frame, char* mod, uint32_t mod_size,
                                         char* text, uint32_t text_size);
NEBULA_GAME_API int32_t luma_guard_faults(const char* mod, uint32_t* count, char* callers, uint32_t callers_size);
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
// A label inside a function: Mach-O cuts the text at each global label unless it is the function's alternate entry.
#define LUMA_GUARD_INSIDE(name) ".alt_entry _" #name "\n"
#else
#define LUMA_GUARD_SYM(name) #name
#define LUMA_GUARD_HIDE(name) ".hidden " #name "\n"
#define LUMA_GUARD_TYPE(name) ".type " #name ", @function\n" LUMA_GUARD_HIDE(name)
#define LUMA_GUARD_INSIDE(name) ""
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
        ".globl " LUMA_GUARD_SYM(luma_guard_call_return) "\n" LUMA_GUARD_HIDE(luma_guard_call_return) LUMA_GUARD_INSIDE(luma_guard_call_return)
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
// A label inside a function: Mach-O cuts the text at each global label unless it is the function's alternate entry.
#define LUMA_GUARD_INSIDE(name) ".alt_entry _" #name "\n"
#else
#define LUMA_GUARD_SYM(name) #name
#define LUMA_GUARD_HIDE(name) ".hidden " #name "\n"
#define LUMA_GUARD_TYPE(name) ".type " #name ", %function\n" LUMA_GUARD_HIDE(name)
#define LUMA_GUARD_INSIDE(name) ""
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
        ".globl " LUMA_GUARD_SYM(luma_guard_call_return) "\n" LUMA_GUARD_HIDE(luma_guard_call_return) LUMA_GUARD_INSIDE(luma_guard_call_return)
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
#error "LumaCore's guard has no call for this processor or system (luma_guard.h; Windows on ARM64)"
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
            // The mod whose code runs on this thread now (current()): the innermost run()'s.
            thread_local LumaMod* tMod = nullptr;

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
                u32 told;          // its fact kernel.guard_time written (harness runs)
                char id[72];       // the mod's id (at most 64 bytes)
                u64 frameTicks;    // this frame's, added to from any thread
                u64 ring[cCostFrames];
                u32 at;
                u32 filled;
                u64 sumTicks;      // over the ring
                u64 worstTicks;
                // Faults in this mod's code under other mods' calls this session (the need walk turned the caller off),
                // and the first two callers it faulted under (luma_guard_faults; the Mods page). caught() writes them on the
                // faulting thread, whichever it is; any thread reads. The count is atomic; a caller's id is written while
                // `writing` is held and published by `callersSet` (release): a reader reads only the ids below it.
                u32 faultsUnder;
                u32 commonSaid;    // the "common place" notice was given (claimed by exchange)
                u32 writing;
                u32 callersSet;
                char callers[2][72];
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
                "R:Guard.Boundary.Before", "R:Guard.Boundary.After", "R:Guard.Boundary.Wrap",     "R:Guard.Boundary.Tell",
                "R:Guard.Boundary.Start",  "R:Guard.Boundary.Stop",  "R:Guard.Boundary.Event",    "R:Guard.Boundary.Class",
                "R:Guard.Boundary.Callback", "R:Guard.Boundary.Cut"};
            const char* const cBoundaryWords[LUMA_GUARD_BOUNDARY_COUNT] = {"before hook", "after hook", "wrap hook", "hook", "start",
                                                                           "stop", "service event", "class", "call from another mod", "cut"};

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

            // Appends text at *pAt within size, NUL-ended; only what a fault handler may use.
            void append(char* pDst, u32 size, u32* pAt, const char* pText) {
                for (; pText != nullptr && *pText != 0 && *pAt + 1 < size; pText++) {
                    pDst[(*pAt)++] = *pText;
                }
                pDst[*pAt < size ? *pAt : size - 1] = 0;
            }

            void copyId(char* pDst, u32 size, const char* pText) {
                u32 at = 0;
                pDst[0] = 0;
                append(pDst, size, &at, pText);
            }

            // ---- The need walk (luma_guard_walk.h; when-a-mod-fails.md, "A fault in another mod's code") ----
            // What the walk found for one fault: the mods the chain passed through, innermost first, and the boundary's
            // mod last; which of them is turned off; whose code faulted.
            struct Blame {
                LumaMod* pMods[LUMA_GUARD_CHAIN + 1];
                uintptr_t pcs[LUMA_GUARD_CHAIN + 1];  // the chain's address of each; 0: the boundary's mod, added
                uintptr_t outers[LUMA_GUARD_CHAIN + 1];  // the outermost frame's address of each run (chain_outer)
                u32 count;
                u32 gap;
                u32 offAt;
                LumaMod* pOff;      // never null: the boundary's mod when the walk names none
                LumaMod* pFaulted;  // null: the fault is in code the kernel cannot name
                uintptr_t pc;       // the fault
                bool outside;       // the fault is outside the mods' code: pFaulted's code called the code that faulted
            };

            bool needs(const LumaMod* pUser, const LumaMod* pUsed) { return modUses(pUser, pUsed); }
            bool needsThrough(const LumaMod* pUser, const LumaMod* pUsed) { return modUsesThrough(pUser, pUsed); }

            // Whether `pc` is in pMod's code.
            bool inCode(const LumaMod* pMod, uintptr_t pc) {
                uintptr_t base = 0, begin = 0, end = 0;
                if (pMod == nullptr) {
                    return false;
                }
                modCode(pMod, &base, &begin, &end);
                return pc >= begin && pc < end;
            }

            // The mod whose code holds `pc`: a listed one (modAt), else the boundary's own mod, which is in no list while
            // the loader runs its begin, start, stop or finish.
            LumaMod* modOf(uintptr_t pc, LumaMod* pBoundary) {
                LumaMod* const pMod = modAt(pc);
                return pMod != nullptr ? pMod : inCode(pBoundary, pc) ? pBoundary : nullptr;
            }

            // From the chain the host wrote (LumaGuardThread.chain) and the boundary's mod. Reads only: the host's
            // handler calls it, through luma_guard_blame, for a fault it does not resume.
            void blameOf(const LumaGuardThread& rThread, LumaMod* pBoundary, uintptr_t faultPc, Blame* pOut) {
                u32 n = 0;
                const u32 count = rThread.chain_count <= LUMA_GUARD_CHAIN ? rThread.chain_count : LUMA_GUARD_CHAIN;
                for (u32 i = 0; i < count; i++) {
                    pOut->pMods[n] = modOf(rThread.chain[i], pBoundary);
                    pOut->pcs[n] = rThread.chain[i];
                    pOut->outers[n] = rThread.chain_outer[i] != 0 ? rThread.chain_outer[i] : rThread.chain[i];
                    n++;
                }
                u32 gap = rThread.chain_gap;
                if (n == 0 && faultPc != 0) {
                    // No chain (the fault was not in mods' code): the fault alone, followed by nothing.
                    pOut->pMods[0] = modOf(faultPc, pBoundary);
                    pOut->pcs[0] = faultPc;
                    pOut->outers[0] = faultPc;
                    n = 1;
                    gap = 0;
                }
                if (pBoundary != nullptr && (n == 0 || pOut->pMods[n - 1] != pBoundary)) {
                    pOut->pMods[n] = pBoundary;
                    pOut->pcs[n] = 0;
                    pOut->outers[n] = 0;
                    n++;
                }
                pOut->count = n;
                pOut->gap = gap;
                pOut->outside = rThread.chain_pc != 0 && (count == 0 || rThread.chain[0] != rThread.chain_pc);
                pOut->pc = pOut->outside ? rThread.chain_pc : n != 0 ? pOut->pcs[0] : faultPc;
                pOut->pFaulted = n != 0 ? pOut->pMods[0] : nullptr;
                u32 at = n != 0 ? luma_guard_walk(pOut->pMods, n, gap, needs, needsThrough) : 0;
                if (n == 0 || pOut->pMods[at] == nullptr) {
                    at = n != 0 ? n - 1 : 0;
                }
                pOut->offAt = at;
                pOut->pOff = n != 0 && pOut->pMods[at] != nullptr ? pOut->pMods[at] : pBoundary;
            }

            // The place of `pc` in its mod's library ("lib.dll+0x1a2b"), or "0x<pc>" when no mod holds it.
            void placeIn(char* pDst, u32 size, const LumaMod* pMod, uintptr_t pc) {
                uintptr_t base = 0, begin = 0, end = 0;
                if (pMod != nullptr) {
                    modCode(pMod, &base, &begin, &end);
                }
                if (pMod != nullptr && pc >= begin && pc < end) {
                    // "<mod>/<file>": a mod's library has the same file name in every mod's folder.
                    char file[128];
                    nebula_snprintf(file, sizeof(file), "%s/%s", modId(pMod), modFile(pMod));
                    placeOf(pDst, size, file, pc - base);
                } else {
                    placeOf(pDst, size, "", pc);
                }
            }

            // The mod the turned-off one called on the way to the fault (the library it called into), or null.
            LumaMod* calledBy(const Blame& rBlame) { return rBlame.offAt > 0 ? rBlame.pMods[rBlame.offAt - 1] : nullptr; }

            // After a resume, on the faulting thread, with the frame already off the stack: the mod the walk names off,
            // said, taken out; the library whose code faulted under it counted.
            void caught(LumaMod* pBoundary, const LumaGuardFrame& rFrame, const Blame& rBlame) {
                LumaMod* const pOff = rBlame.pOff;
                __atomic_store_n(modFaulted(pOff), 1u, __ATOMIC_RELEASE);
                const u32 kind = rFrame.boundary < LUMA_GUARD_BOUNDARY_COUNT ? rFrame.boundary : LUMA_GUARD_START;
                const char* const pOffId = modId(pOff);
                char place[160];
                placeIn(place, sizeof(place), rBlame.pFaulted, rBlame.pc);
                char address[24];
                placeOf(address, sizeof(address), "", rFrame.address);
                // Whose call it was, when not the turned-off mod's own boundary: a library's hook that called its code.
                char under[120];
                u32 at = 0;
                under[0] = 0;
                if (pBoundary != nullptr && pBoundary != pOff) {
                    append(under, sizeof(under), &at, ", in ");
                    append(under, sizeof(under), &at, modId(pBoundary));
                    append(under, sizeof(under), &at, "'s call");
                }
                LumaMod* const pCalled = calledBy(rBlame);
                const char* const pFaultedId = rBlame.pFaulted != nullptr ? modId(rBlame.pFaulted) : "";
                LumaModLogEntry entry = {};
                entry.size = sizeof(entry);
                entry.source = LUMA_MODLOG_GUARD;
                entry.level = LUMA_MODLOG_ERROR;
                entry.code = rFrame.fault;
                entry.mod = pOffId;
                entry.file = rFrame.where;
                entry.names[0] = "boundary";
                entry.values[0] = cBoundaryRows[kind];
                entry.names[1] = "where";
                entry.values[1] = rFrame.where;
                entry.names[2] = "fault";
                entry.values[2] = faultRow(rFrame.fault);
                entry.names[3] = "address";
                entry.values[3] = place;
                entry.names[4] = "under";
                entry.values[4] = pBoundary != nullptr && pBoundary != pOff ? modId(pBoundary) : "";
                entry.count = 5;
                if (pCalled == nullptr || rBlame.pFaulted == nullptr || rBlame.pFaulted == pOff) {
                    // Its own code faulted.
                    nebula_log("[mod %s] GUARD: its code failed (%s: %s%s): %s at %s, address %s. It is off until the next start; "
                               "what it was changing may be half done.\n",
                               pOffId, cBoundaryWords[kind], rFrame.where, under, faultWords(rFrame.fault), place, address);
                    luma::modlog::add(entry, pBoundary != nullptr && pBoundary != pOff ? "Guard.CaughtUnder" : "Guard.Caught");
                } else {
                    // Code of a mod it needs faulted under its call: it is turned off; the library stays.
                    char called[120];
                    at = 0;
                    called[0] = 0;
                    append(called, sizeof(called), &at, modId(pCalled));
                    if (pCalled != rBlame.pFaulted) {
                        append(called, sizeof(called), &at, " (in ");
                        append(called, sizeof(called), &at, pFaultedId);
                        append(called, sizeof(called), &at, "'s code)");
                    }
                    nebula_log("[mod %s] GUARD: its call into %s failed (%s: %s%s): %s at %s, address %s. It is off until the next "
                               "start; what it was changing may be half done. %s stays on.\n",
                               pOffId, called, cBoundaryWords[kind], rFrame.where, under, faultWords(rFrame.fault), place, address,
                               pFaultedId);
                    entry.names[5] = "library";
                    entry.values[5] = modId(pCalled);
                    entry.names[6] = "faulted";
                    entry.values[6] = pFaultedId;
                    entry.count = 7;
                    luma::modlog::add(entry, pCalled != rBlame.pFaulted ? "Guard.CaughtInDeeper" : "Guard.CaughtIn");
                }
                // Its hooks go at the next frame's start, with the mods that use it; its stop and finish do not run. A
                // fault in its own begin or start: the loader refuses it there and then; in its stop or finish: it is going
                // already.
                const bool ownLifecycle = pOff == pBoundary && (kind == LUMA_GUARD_START ||
                                                                (kind == LUMA_GUARD_STOP && (same(rFrame.where, "stop") || same(rFrame.where, "finish"))));
                if (!ownLifecycle) {
                    requestTakeOut(pOffId);
                }
                // A library whose code faulted under another mod's call: counted; under a second caller, the common place.
                if (rBlame.pFaulted != nullptr && rBlame.pFaulted != pOff) {
                    CostSlot* const pSlot = costSlotOf(rBlame.pFaulted);
                    if (pSlot != nullptr) {
                        const u32 count = __atomic_add_fetch(&pSlot->faultsUnder, 1u, __ATOMIC_RELAXED);
                        while (__atomic_exchange_n(&pSlot->writing, 1u, __ATOMIC_ACQUIRE) != 0u) {
                        }
                        const u32 had = __atomic_load_n(&pSlot->callersSet, __ATOMIC_RELAXED);
                        if (had == 0u) {
                            copyId(pSlot->callers[0], sizeof(pSlot->callers[0]), pOffId);
                            __atomic_store_n(&pSlot->callersSet, 1u, __ATOMIC_RELEASE);
                        } else if (had == 1u && !same(pSlot->callers[0], pOffId)) {
                            copyId(pSlot->callers[1], sizeof(pSlot->callers[1]), pOffId);
                            __atomic_store_n(&pSlot->callersSet, 2u, __ATOMIC_RELEASE);
                        }
                        __atomic_store_n(&pSlot->writing, 0u, __ATOMIC_RELEASE);
                        const u32 set = __atomic_load_n(&pSlot->callersSet, __ATOMIC_ACQUIRE);
                        const char* const pFirst = set >= 1u ? pSlot->callers[0] : "";
                        const char* const pSecond = set >= 2u ? pSlot->callers[1] : "";
                        if (luma::modlog::facts()) {
                            // guard.under.<mod>: the count and the first two callers (a test reads them).
                            char key[96];
                            nebula_snprintf(key, sizeof(key), "guard.under.%s", pFaultedId);
                            char json[256];
                            nebula_snprintf(json, sizeof(json), "{\"count\": %u, \"first\": \"%s\", \"second\": \"%s\"}", count,
                                            pFirst, pSecond);
                            luma::modlog::fact(key, json);
                        }
                        if (set >= 2u && __atomic_exchange_n(&pSlot->commonSaid, 1u, __ATOMIC_ACQ_REL) == 0u) {
                            nebula_log("[mod %s] GUARD: its code failed under two mods' calls, %s's and %s's: the fault is likely its "
                                       "own. Both are off; it stays on. On the Mods page it can be turned off, or they on again.\n",
                                       pFaultedId, pFirst, pSecond);
                            LumaModLogEntry common = {};
                            common.size = sizeof(common);
                            common.source = LUMA_MODLOG_GUARD;
                            common.level = LUMA_MODLOG_ERROR;
                            common.code = rFrame.fault;
                            common.mod = pFaultedId;
                            common.file = "";
                            common.names[0] = "first";
                            common.values[0] = pFirst;
                            common.names[1] = "second";
                            common.values[1] = pSecond;
                            common.count = 2;
                            luma::modlog::add(common, "Guard.CommonPlace");
                        }
                    }
                }
            }

            // Every mod that stays on with code on the cut stack is told (LumaModInfo.cut), the innermost first, each
            // once: its innermost cut function and place, and who called it there. A mod that uses the one turned off goes
            // with it at the next frame and is not told. Runs after `handling` is cleared: a fault in a cut is caught as any.
            void tellCut(const LumaGuardFrame& rFrame, const Blame& rBlame) {
                const char* const pOffId = modId(rBlame.pOff);
                const char* const pFaultedId = rBlame.pFaulted != nullptr ? modId(rBlame.pFaulted) : "";
                for (u32 i = 0; i < rBlame.count; i++) {
                    LumaMod* const pMod = rBlame.pMods[i];
                    if (pMod == nullptr || rBlame.pcs[i] == 0 || pMod == rBlame.pOff || faulted(pMod) || modUsesThrough(pMod, rBlame.pOff)) {
                        continue;
                    }
                    bool told = false;
                    for (u32 j = 0; j < i && !told; j++) {
                        told = rBlame.pMods[j] == pMod;
                    }
                    if (told) {
                        continue;
                    }
                    LumaMod* const pCaller = i + 1 < rBlame.count && rBlame.pcs[i + 1] != 0 ? rBlame.pMods[i + 1] : nullptr;
                    char place[160];
                    placeIn(place, sizeof(place), pMod, rBlame.pcs[i]);
                    LumaCut cut = {};
                    cut.size = sizeof(cut);
                    cut.boundary = rFrame.boundary;
                    cut.where = rFrame.where;
                    cut.stack = static_cast<uintptr_t>(rFrame.regs[LUMA_GUARD_SP]);
                    // Named by the function its caller entered (the run's outermost frame): an export, or "".
                    cut.function = modExportAt(pMod, sHost != nullptr ? sHost->function_at(rBlame.outers[i]) : 0);
                    cut.place = place;
                    cut.caller = pCaller;
                    cut.caller_id = pCaller != nullptr ? modId(pCaller) : "";
                    cut.off = pOffId;
                    cut.faulted = pFaultedId;
                    const LumaModInfo* const pInfo = modInfo(pMod);
                    const bool hasCut = pInfo != nullptr && pInfo->size >= offsetof(LumaModInfo, cut) + sizeof(pInfo->cut) && pInfo->cut != nullptr;
                    const char* const pFunction = *cut.function != 0 ? cut.function : "its code";
                    if (hasCut) {
                        nebula_log("[mod %s] a fault in %s's code cut %s short (at %s, called by %s); its cut is told\n", modId(pMod),
                                   *pFaultedId != 0 ? pFaultedId : "a mod", pFunction, place, pCaller != nullptr ? cut.caller_id : "the game");
                        run(pMod, LUMA_GUARD_CUT, "cut", reinterpret_cast<const void*>(pInfo->cut), reinterpret_cast<uintptr_t>(&cut), 0, 0, 0,
                            nullptr);
                    } else {
                        nebula_log("[mod %s] a fault in %s's code cut %s short (at %s, called by %s). It has no cut, so what that call "
                                   "was changing may be half done; it stays on.\n",
                                   modId(pMod), *pFaultedId != 0 ? pFaultedId : "a mod", pFunction, place,
                                   pCaller != nullptr ? cut.caller_id : "the game");
                        LumaModLogEntry entry = {};
                        entry.size = sizeof(entry);
                        entry.source = LUMA_MODLOG_GUARD;
                        entry.level = LUMA_MODLOG_ERROR;
                        entry.code = rFrame.fault;
                        entry.mod = modId(pMod);
                        entry.file = rFrame.where;
                        entry.names[0] = "faulted";
                        entry.values[0] = pFaultedId;
                        entry.names[1] = "address";
                        entry.values[1] = place;
                        entry.count = 2;
                        luma::modlog::add(entry, "Guard.CutUntold");
                    }
                }
            }

            // run()'s resume, out of run's own frame so every boundary's frame stays small.
            __attribute__((noinline)) void afterFault(LumaMod* pMod, LumaGuardThread* pThread, const LumaGuardFrame& rFrame) {
                Blame blame;
                blameOf(*pThread, pMod, rFrame.pc, &blame);
                // The chain is read: a fault from here (in a cut) is caught as any other.
                pThread->handling = 0u;
                caught(pMod, rFrame, blame);
                tellCut(rFrame, blame);
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
            LumaMod* const pOuterMod = tMod;
            tMod = pMod;
            // The frame is complete before the host's handler, on this thread, can see it.
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
            pThread->top = &frame;
            __atomic_signal_fence(__ATOMIC_SEQ_CST);
            const u64 start = ticks();
            const bool returned = luma_guard_call(&frame) == 0;
            const u64 total = ticks() - start;
            charge(pMod, total > tNestedTicks ? total - tNestedTicks : 0);
            tNestedTicks = outerNested + total;
            tMod = pOuterMod;
            if (returned) {
                pThread->top = frame.outer;
                if (pResult != nullptr) {
                    *pResult = frame.result;
                }
                return true;
            }
            // Resumed: the host wrote the fault into the frame and the chain into the thread, and set `handling`.
            pThread->top = frame.outer;
            afterFault(pMod, pThread, frame);
            return false;
        }

        int32_t callMod(const LumaMod* pCaller, const void* fn, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t* pResult) {
            if (pResult != nullptr) {
                *pResult = 0;
            }
            LumaMod* const pGiver = fn != nullptr ? modAt(reinterpret_cast<uintptr_t>(fn)) : nullptr;
            if (pCaller == nullptr || pGiver == nullptr) {
                nebula_log("[mod %s] call_mod: %p is in no running mod's code; it was not called\n",
                           pCaller != nullptr ? modId(pCaller) : "(none)", fn);
                return -4;
            }
            if (pGiver == pCaller || modUsesThrough(pCaller, pGiver)) {
                // A call down into its own code or a library it needs: no boundary (a fault there is the caller's).
                const uint64_t result = reinterpret_cast<uint64_t (*)(uint64_t, uint64_t, uint64_t, uint64_t)>(const_cast<void*>(fn))(a0, a1, a2, a3);
                if (pResult != nullptr) {
                    *pResult = result;
                }
                return 0;
            }
            if (faulted(pGiver)) {
                return -2;
            }
            u64 result = 0;
            if (!run(pGiver, LUMA_GUARD_CALLBACK, modId(pCaller), fn, a0, a1, a2, a3, &result)) {
                return faulted(pGiver) ? -1 : -2;
            }
            if (pResult != nullptr) {
                *pResult = result;
            }
            return 0;
        }

        LumaMod* current() { return tMod; }

        void measureForHarness() {
            // Once, in a harness run only (the facts writer is set): the boundary's cost against a plain call of the same
            // empty function, on this thread, through the same frame fill, push, luma_guard_call and pop as run().
            static_assert(sizeof(LumaGuardFrame) <= 512, "the frame stays small");
            if (sMeasured || !luma::modlog::facts()) {
                return;
            }
            sMeasured = true;
            // In ticks, the boundaries' own clock: the host's is the frames' in a deterministic (harness) run, and does
            // not move within this call.
            const u32 cCount = 100000;
            u64 (*volatile plain)(uintptr_t, uintptr_t) = nothing;
            u64 sink = 0;
            const u64 t0 = ticks();
            for (u32 i = 0; i < cCount; i++) {
                sink += plain(i, 0);
            }
            const u64 t1 = ticks();
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
                LumaMod* const pOuterMod = tMod;  // as run() keeps it
                tMod = nullptr;
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
                tMod = pOuterMod;
                pThread->top = frame.outer;
            }
            const u64 t2 = ticks();
            // The clock's pair of reads alone, as each boundary makes it.
            u64 pairs = 0;
            for (u32 i = 0; i < cCount; i++) {
                const u64 a = ticks();
                pairs += ticks() - a;
            }
            const u64 t3 = ticks();
            sink += measured + pairs;
            // Picoseconds per tick when the clock is scaled (arm64 at install; x64 a quarter second after it), else 0.
            const unsigned long long psPerTick = static_cast<unsigned long long>(nsPerTick() * 1000.0);
            // The kernel's fact (not guard.<mod id>: a mod's guard event).
            char json[240];
            nebula_snprintf(json, sizeof(json),
                            "{\"count\": %u, \"plain_ticks\": %llu, \"guarded_ticks\": %llu, \"clock_pairs_ticks\": %llu, "
                            "\"ps_per_tick\": %llu, \"sink\": %llu}",
                            cCount, static_cast<unsigned long long>(t1 - t0), static_cast<unsigned long long>(t2 - t1),
                            static_cast<unsigned long long>(t3 - t2), psPerTick, static_cast<unsigned long long>(sink));
            luma::modlog::fact("kernel.guard_cost", json);
            nebula_log("LumaCore: the guard's boundary costs %llu ticks per call more than a plain call, its clock's pair of reads "
                       "%llu ticks of that (%u calls: %llu ticks against %llu; %u pairs: %llu ticks; %llu ps per tick, 0: not "
                       "scaled yet)\n",
                       static_cast<unsigned long long>(t2 - t1 > t1 - t0 ? (t2 - t1 - (t1 - t0)) / cCount : 0),
                       static_cast<unsigned long long>((t3 - t2) / cCount), cCount, static_cast<unsigned long long>(t2 - t1),
                       static_cast<unsigned long long>(t1 - t0), cCount, static_cast<unsigned long long>(t3 - t2), psPerTick);
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
                // In a harness run, once per load: the first full ring as luma_guard_cost gives it.
                if (rSlot.filled == cCostFrames && rSlot.told == 0 && luma::modlog::facts()) {
                    rSlot.told = 1;
                    uint64_t avgNs = 0;  // luma_guard_cost's type (unsigned long on Android, not u64's long long)
                    uint64_t worstNs = 0;
                    const int32_t found = luma_guard_cost(rSlot.id, &avgNs, &worstNs);
                    char json[200];
                    nebula_snprintf(json, sizeof(json), "{\"mod\": \"%s\", \"found\": %d, \"avg_ns\": %llu, \"worst_ns\": %llu}", rSlot.id,
                                    static_cast<int>(found), static_cast<unsigned long long>(avgNs),
                                    static_cast<unsigned long long>(worstNs));
                    luma::modlog::fact("kernel.guard_time", json);
                }
            }
        }
    }  // namespace guard
}  // namespace luma

extern "C" {
NEBULA_GAME_API int32_t luma_guard_install(const LumaGuardHost* host) {
    return luma::guard::install(host) ? 1 : 0;
}

// A fault the host does not resume: the mod the next start turns off and the words the crash note carries. Reads only.
NEBULA_GAME_API int32_t luma_guard_blame(const LumaGuardThread* thread, const LumaGuardFrame* frame, char* mod, uint32_t mod_size,
                                         char* text, uint32_t text_size) {
    using namespace luma::guard;
    if (mod == nullptr || text == nullptr || mod_size == 0 || text_size == 0) {
        return 0;
    }
    mod[0] = 0;
    text[0] = 0;
    if (thread == nullptr || frame == nullptr) {
        return 0;
    }
    // The boundary's mod: by its code, or (in its begin, start, stop or finish, when no list holds it) the thread's
    // current mod, which run() set for this boundary.
    LumaMod* pBoundary = frame->code_begin != 0 ? luma::modAt(frame->code_begin) : nullptr;
    if (pBoundary == nullptr && inCode(current(), frame->code_begin)) {
        pBoundary = current();
    }
    Blame blame;
    blameOf(*thread, pBoundary, thread->chain_pc, &blame);
    if (blame.pOff == nullptr) {
        return 0;
    }
    copyId(mod, mod_size, luma::modId(blame.pOff));
    char place[160];
    u32 at = 0;
    append(text, text_size, &at, "turned off: ");
    append(text, text_size, &at, luma::modId(blame.pOff));
    if (blame.outside && blame.count != 0 && blame.pcs[0] != 0 && blame.pFaulted != nullptr) {
        // The game's or the system's code faulted, called from a mod's.
        placeIn(place, sizeof(place), blame.pFaulted, blame.pcs[0]);
        append(text, text_size, &at, "; the fault outside the mods' code, called from ");
        append(text, text_size, &at, luma::modId(blame.pFaulted));
        append(text, text_size, &at, "'s code at ");
    } else {
        placeIn(place, sizeof(place), blame.pFaulted, blame.pc);
        append(text, text_size, &at, "; the fault in ");
        append(text, text_size, &at, blame.pFaulted != nullptr ? luma::modId(blame.pFaulted) : "code no mod's");
        append(text, text_size, &at, blame.pFaulted != nullptr ? "'s code at " : " at ");
    }
    append(text, text_size, &at, place);
    return 1;
}

// Faults in a mod's code under other mods' calls this session (when-a-mod-fails.md): their count, and the first two
// callers' ids, each NUL-ended, into callers (an empty one: none). 0: found; -1: no such mod this session. Any thread.
NEBULA_GAME_API int32_t luma_guard_faults(const char* mod, uint32_t* count, char* callers, uint32_t callers_size) {
    using namespace luma::guard;
    if (count != nullptr) {
        *count = 0;
    }
    if (callers != nullptr && callers_size != 0) {
        callers[0] = 0;
    }
    if (mod == nullptr) {
        return -1;
    }
    const u32 used = __atomic_load_n(&sCostUsed, __ATOMIC_ACQUIRE);
    for (u32 s = 0; s < used; s++) {
        const CostSlot& rSlot = sCostSlots[s];
        if (!same(rSlot.id, mod)) {
            continue;
        }
        if (count != nullptr) {
            *count = __atomic_load_n(&rSlot.faultsUnder, __ATOMIC_RELAXED);
        }
        if (callers != nullptr && callers_size != 0) {
            const u32 set = __atomic_load_n(&rSlot.callersSet, __ATOMIC_ACQUIRE);
            u32 at = 0;
            append(callers, callers_size, &at, set >= 1u ? rSlot.callers[0] : "");
            if (at + 1 < callers_size) {
                callers[at++] = 0;
                callers[at] = 0;
                u32 second = 0;
                append(callers + at, callers_size - at, &second, set >= 2u ? rSlot.callers[1] : "");
            }
        }
        return 0;
    }
    return -1;
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
