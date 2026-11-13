/* SPDX-License-Identifier: GPL-3.0-only */
/* The guard (plan 1.6, contract guard): between the game's LumaCore kernel and the host; not for mods.

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
    LUMA_GUARD_BOUNDARY_COUNT = 8
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

/* One per thread that ever ran a mod's code (the module's thread_local), given to the host once (thread_enter). */
typedef struct LumaGuardThread {
    LumaGuardFrame* volatile top; /* the innermost boundary; null outside every one */
    volatile uint32_t handling;   /* 1 from a resume until the kernel finished with that fault: a fault meanwhile is
                                     not resumed */
    uint32_t entered;             /* the module's own: thread_enter was asked */
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
} LumaGuardHost;

/* Where the stack pointer saved in regs is: the index per calling convention (MSVC names x64 _M_X64). */
#if defined(_WIN32) && (defined(__x86_64__) || defined(_M_X64))
#define LUMA_GUARD_SP 8
#elif defined(__x86_64__)
#define LUMA_GUARD_SP 6
#elif defined(__aarch64__) || defined(_M_ARM64)
#define LUMA_GUARD_SP 12
#endif

/* The game module's export "luma_guard_install": 1 taken; 0 not (a host struct too small). */
typedef int32_t (*LumaGuardInstall)(const LumaGuardHost* host);

/* The game module's export "luma_guard_cost" (the director's order of 11-11; J's CODE TIME on a mod's page): the time
   the mod's code took per frame over its last 120 frames (each boundary's own time, less the boundaries nested in it:
   another mod's hook its call reached), in ns: the average frame and the worst one. Any thread. 0: found; -1: no such
   mod this session, or the clock not yet scaled (the first quarter second). A mod taken out keeps its last values. */
typedef int32_t (*LumaGuardCost)(const char* mod, uint64_t* avg_ns, uint64_t* worst_ns);

#ifdef __cplusplus
}
#endif

#endif
