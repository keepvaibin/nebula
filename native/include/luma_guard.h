/* SPDX-License-Identifier: GPL-3.0-only */
/* The guard: between the game's LumaCore kernel and the host; not for mods.

   At every place the kernel runs a mod's code (a boundary) it pushes a frame on the thread's stack of frames and calls
   the mod's function through luma_guard_call, which saves the registers the platform's calling convention keeps across
   a call and then calls the function itself, so nothing of the kernel lies between the frame and the mod's code. The
   host's fault handler (Windows: a vectored handler; POSIX: sigaction on an alternate stack) reads the faulting
   thread's innermost frame and, only when it is safe, resumes there: luma_guard_call returns a second time, with 1,
   and the kernel goes on as if the mod's code had done nothing, the mod off for the session. Everything else passes on
   untouched. */
#ifndef LUMA_GUARD_H
#define LUMA_GUARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Where the kernel ran the mod's code. */
typedef enum LumaGuardBoundary {
    LUMA_GUARD_BEFORE = 0, /* a before hook */
    LUMA_GUARD_AFTER = 1,  /* an after hook */
    LUMA_GUARD_WRAP = 2,   /* a wrap or replace hook */
    LUMA_GUARD_TELL = 3,   /* a hook told of a call (no result) */
    LUMA_GUARD_START = 4,  /* begin, start */
    LUMA_GUARD_STOP = 5,   /* stop, finish, dependent_gone */
    LUMA_GUARD_EVENT = 6,  /* a service's event or timer */
    LUMA_GUARD_CLASS = 7,  /* a made class: made, its hooks, an override */
    LUMA_GUARD_CALLBACK = 8, /* a library mod's call of code another mod gave it (LumaCore.call_mod) */
    LUMA_GUARD_CUT = 9,      /* the mod's `cut`: a fault cut a call of its code short (LumaModInfo.cut) */
    LUMA_GUARD_BOUNDARY_COUNT = 10
} LumaGuardBoundary;

/* What the host caught. Only the first three are resumed. */
typedef enum LumaGuardFault {
    LUMA_GUARD_NONE = 0,
    LUMA_GUARD_ACCESS = 1,  /* memory it may not touch (access violation, SIGSEGV, SIGBUS) */
    LUMA_GUARD_ILLEGAL = 2, /* an instruction the processor refuses (SIGILL, a privileged one) */
    LUMA_GUARD_DIVIDE = 3,  /* integer division by zero or its overflow (SIGFPE) */
    LUMA_GUARD_STACK = 4    /* the stack overflowed: never resumed */
} LumaGuardFault;

#define LUMA_GUARD_REGS 32

typedef struct LumaGuardFrame {
    /* luma_guard_call's: per calling convention the registers kept across a call, the stack pointer as it is after the
       call returns, and the return address (win-x64: rbx rbp rdi rsi r12-r15 rsp rip mxcsr/fpcw xmm6-15; sysv-x64: rbx
       rbp r12-r15 rsp rip mxcsr/fpcw; aarch64: x19-x30 sp d8-d15 fpcr). */
    uint64_t regs[LUMA_GUARD_REGS];
    /* The call: the mod's function, its first four integer or pointer arguments, its integer result. */
    void* fn;
    uint64_t args[4];
    uint64_t result;
    struct LumaGuardFrame* outer; /* the boundary this one runs inside, on the same thread; null: none */
    const char* mod;              /* the mod's id */
    const char* where;            /* the function, service or class whose code the boundary is about */
    uint32_t boundary;            /* LumaGuardBoundary */
    uint32_t fault;               /* LumaGuardFault: written by the host before it resumes */
    uintptr_t code_begin;         /* the mod library's image: a fault is resumed only inside it */
    uintptr_t code_end;
    uintptr_t pc;                 /* written by the host: the faulting instruction */
    uintptr_t address;            /* written by the host: the address it touched (0 when none) */
    void (*resume)(struct LumaGuardFrame* frame); /* the kernel's luma_guard_resume: restores regs, returns 1 */
    uintptr_t call_return; /* the address right after luma_guard_call's call of fn (a value the mod's frames hold) */
} LumaGuardFrame;

/* The most mods' code a fault's call chain records (LumaGuardThread.chain). */
#define LUMA_GUARD_CHAIN 32

/* One per thread that ever ran a mod's code (the module's thread_local), given to the host once (thread_enter). */
typedef struct LumaGuardThread {
    LumaGuardFrame* volatile top; /* the innermost boundary; null outside every one */
    volatile uint32_t handling;   /* 1 from a resume until the kernel finished with that fault: a fault meanwhile is
                                     not resumed */
    uint32_t entered;             /* the module's own: thread_enter was asked */
    /* Written by the host at a fault inside a boundary, before it resumes or reports it: the mods' code the fault's call
       chain passed through between the fault and the innermost boundary, one address for each run of frames in one
       mod library's code, innermost first. chain[0] is the faulting instruction; each next one the return address into
       the code that called the run before it. A mod library is a span of the host's (add_library), the boundary's own
       code included. chain_count entries; chain_gap: the index after which frames were not followed one by one (more
       runs than LUMA_GUARD_CHAIN, whose middle is dropped, or a stack read word by word, where only the fault is
       recorded), else LUMA_GUARD_CHAIN. For a fault outside the mods' code (in the game's or the system's, which a
       mod's code called), chain[0] is the innermost mod frame's return address instead and chain_pc tells them apart.
       chain_pc: the faulting instruction. chain_outer[i]: the run's outermost frame's address (the function its caller
       entered: an export, a callback), chain[i]'s own for a run of one frame. */
    uintptr_t chain[LUMA_GUARD_CHAIN];
    uint32_t chain_count;
    uint32_t chain_gap;
    uintptr_t chain_pc;
    uintptr_t chain_outer[LUMA_GUARD_CHAIN];
} LumaGuardThread;

/* The host's half, given once by luma_guard_install before any mod loads. */
typedef struct LumaGuardHost {
    uint32_t size; /* sizeof(LumaGuardHost) as the host knows it */
    /* Before the thread's first boundary, on that thread: the host keeps `thread` where its handler reads it without
       the module's thread-local storage (TlsSetValue, pthread_setspecific) and, on POSIX, gives the thread its
       alternate signal stack. 0: guarded; -1: not (said in the log; the thread's faults reach the crash report). */
    int32_t (*thread_enter)(LumaGuardThread* thread);
    /* A mod's library (the `library` luma_load_mod got): the image's base (where offsets in it count from) and its
       code, the first byte and the byte past the last of its executable part. 0: found; -1: not (that mod's faults are
       never resumed). */
    int32_t (*code_range)(void* library, uintptr_t* base, uintptr_t* begin, uintptr_t* end);
    /* The first byte of the function whose code holds `pc`, from its image's unwind data (Windows: the function table;
       Linux and Android: .eh_frame_hdr's table), or 0 when the platform's data cannot tell. The kernel names a cut
       function by it (LumaCut.function). Any thread; not from a fault handler. */
    uintptr_t (*function_at)(uintptr_t pc);
} LumaGuardHost;

/* Where the stack pointer saved in regs is: the index per calling convention (MSVC names x64 _M_X64).
   LUMA_GUARD_CALL_BYTES: the bytes just below that stack pointer that are luma_guard_call's own while it calls the mod
   (its saved registers, and the call's return address where the processor pushes one); the mod's frames are below
   them. win-x64: return address, rbx, 32 bytes of shadow space, the call's return slot. sysv-x64: return address,
   rbx, the call's return slot. aarch64: fp, lr, x19 and padding (the call's return address is in lr). */
#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
#define LUMA_GUARD_SP 8
#define LUMA_GUARD_CALL_BYTES 56
#elif defined(__x86_64__)
#define LUMA_GUARD_SP 6
#define LUMA_GUARD_CALL_BYTES 24
#elif defined(__aarch64__) || defined(_M_ARM64)
#define LUMA_GUARD_SP 12
#define LUMA_GUARD_CALL_BYTES 32
#endif

/* The game module's export "luma_guard_install": 1 taken; 0 not (a host struct too small). */
typedef int32_t (*LumaGuardInstall)(const LumaGuardHost* host);

/* The game module's export "luma_guard_blame", for a fault the host does not resume (the game ends): from the chain the
   host wrote into `thread` and the innermost frame, the mod the next start turns off (the need walk, luma_guard_walk.h)
   into `mod` (its id, NUL-ended, at most mod_size bytes), and into `text` the words naming it, the mod whose code
   faulted and the place as that mod's file+offset. Only what a fault handler may use: it reads, never locks or
   allocates. 1: named; 0: no mod could be named (both empty). */
typedef int32_t (*LumaGuardBlame)(const LumaGuardThread* thread, const LumaGuardFrame* frame, char* mod, uint32_t mod_size,
                                  char* text, uint32_t text_size);

/* The game module's export "luma_guard_cost" (CODE TIME on a mod's page in Mods): the time
   the mod's code took per frame over its last 120 frames (each boundary's own time, less the boundaries nested in it:
   another mod's hook its call reached), in ns: the average frame and the worst one. Any thread. 0: found; -1: no such
   mod this session, or the clock not yet scaled (the first quarter second). A mod taken out keeps its last values. */
typedef int32_t (*LumaGuardCost)(const char* mod, uint64_t* avg_ns, uint64_t* worst_ns);

#ifdef __cplusplus
}
#endif

#endif
