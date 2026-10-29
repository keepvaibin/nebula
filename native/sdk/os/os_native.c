/* The native OS core: what lies under the RVL SDK's own scheduler, alarms and synchronisation (OSThread.c,
   OSMutex.c, OSMessage.c, OSAlarm.c, OSTime.c, OSArena.c and OSAlloc.c, compiled as they are).

   All game threads run on one host thread, the simulation thread, one at a time, as on the console's single core:
   the SDK's SelectThread picks the thread, and a switch is a stack switch (context_switch.c). Interrupts (the
   decrementer, which drives OSAlarm, and device interrupts raised by host threads) are delivered on the
   simulation thread whenever the game enables them - at the end of every OS call that disabled them - with
   interrupts disabled during the handler, as the console runs its handlers. Re-enabling is the hottest OS path (every
   heap allocation takes a mutex), so all pending work is one word and the check is one load
   (docs/NATIVE_PERFORMANCE.md). The decrementer is a host clock thread that sets its bit in that word when the
   deadline passes. When no thread can run, the simulation thread sleeps until a host event instead of spinning. */
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <revolution/os.h>

#include <nebula_game_api.h>

#include "nebula_sdk.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <time.h>
#endif

void* nebula_prepare_stack(void* top, size_t size, void (*entry)(void*), void* arg);
void nebula_switch_stack(void** from_sp, void* to_sp);

/* AddressSanitizer test builds: every stack switch is a fiber switch it is told of. */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#include <sanitizer/common_interface_defs.h>
#define NEBULA_ASAN_START(save, bottom, size) __sanitizer_start_switch_fiber((save), (bottom), (size))
#define NEBULA_ASAN_FINISH(save) __sanitizer_finish_switch_fiber((save), NULL, NULL)
#endif
#endif
#ifndef NEBULA_ASAN_START
#define NEBULA_ASAN_START(save, bottom, size) ((void)0)
#define NEBULA_ASAN_FINISH(save) ((void)0)
#endif

extern volatile u32 RunQueueBits;
void __OSThreadInit(void);
void __OSInitAlarm(void);
void __OSReschedule(void);
void OSSetMEM1ArenaLo(void*);
void OSSetMEM1ArenaHi(void*);
void OSSetMEM2ArenaLo(void*);

/* The host's log takes whole lines (stderr before the host attaches): a report without its newline waits for the rest
   of its line. */
static char sLogLine[1024];
static size_t sLogLength;
static atomic_flag sLogLock = ATOMIC_FLAG_INIT;

static void log_emit(void) {
    const NebulaHostServices* host = nebula_host();
    sLogLine[sLogLength] = '\0';
    if (host != NULL && host->log != NULL) {
        host->log(host->user, sLogLine);
    } else {
        fputs(sLogLine, stderr);
        fputc('\n', stderr);
        fflush(stderr);
    }
    sLogLength = 0;
}

void nebula_vlog(const char* format, va_list args) {
    char text[1024];
    const int n = vsnprintf(text, sizeof(text), format, args);
    const size_t length = n < 0 ? 0 : ((size_t)n < sizeof(text) ? (size_t)n : sizeof(text) - 1);
    while (atomic_flag_test_and_set_explicit(&sLogLock, memory_order_acquire)) {
    }
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\n') {
            log_emit();
            continue;
        }
        if (sLogLength == sizeof(sLogLine) - 1) {
            log_emit();
        }
        sLogLine[sLogLength++] = text[i];
    }
    atomic_flag_clear_explicit(&sLogLock, memory_order_release);
}

void nebula_log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    nebula_vlog(format, args);
    va_end(args);
}

static void __attribute__((noreturn)) fatal_text(const char* text) {
    const NebulaHostServices* host = nebula_host();
    if (host != NULL && host->fatal != NULL) {
        host->fatal(host->user, text);
    }
    fprintf(stderr, "Nebula: fatal: %s\n", text);
    fflush(stderr);
    abort();
}

void nebula_fatal(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    fatal_text(text);
}

/* Time base: 60.75 MHz (the console's bus clock / 4), counted from boot. The host counter is scaled by a 64.64
   fixed-point ratio computed once, so a read is the host counter and two multiplies. */

static uint64_t sScaleWhole, sScaleFraction;

#if defined(_WIN32)
static LARGE_INTEGER sClockStart;
#else
static struct timespec sClockStart;
#endif

/* OS_TIMER_CLOCK / rate as whole and 64-bit fractional parts (long division, once). */
static void set_scale(uint64_t rate) {
    sScaleWhole = (uint64_t)OS_TIMER_CLOCK / rate;
    uint64_t remainder = (uint64_t)OS_TIMER_CLOCK % rate;
    uint64_t fraction = 0;
    for (int bit = 63; bit >= 0; bit--) {
        remainder <<= 1; /* remainder < rate < 2^63, so no bit is lost */
        if (remainder >= rate) {
            remainder -= rate;
            fraction |= 1ull << bit;
        }
    }
    sScaleFraction = fraction;
}

static int64_t scale_to_time_base(uint64_t count) {
    return (int64_t)(count * sScaleWhole + (uint64_t)(((unsigned __int128)count * sScaleFraction) >> 64));
}

static void time_init(void) {
#if defined(_WIN32)
    LARGE_INTEGER rate;
    QueryPerformanceFrequency(&rate);
    set_scale((uint64_t)rate.QuadPart);
    QueryPerformanceCounter(&sClockStart);
#else
    set_scale(1000000000u);
    clock_gettime(CLOCK_MONOTONIC, &sClockStart);
#endif
}

int64_t nebula_time_base(void) {
#if defined(_WIN32)
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return scale_to_time_base((uint64_t)(now.QuadPart - sClockStart.QuadPart));
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const int64_t ns = (int64_t)(now.tv_sec - sClockStart.tv_sec) * 1000000000 + (now.tv_nsec - sClockStart.tv_nsec);
    return scale_to_time_base((uint64_t)ns);
#endif
}

static int64_t game_time(void);

/* The time base counts from the local calendar time at boot (ticks since 2000-01-01), as the console's does: the
   system sets it from the clock at boot, so OSGetTime is the date (the game stamps saves with it). */
static int64_t sCalendarBase;

static void calendar_init(void) {
    const time_t now = time(NULL);
    struct tm local;
#if defined(_WIN32)
    localtime_s(&local, &now);
    const time_t fields = _mkgmtime(&local);
#else
    localtime_r(&now, &local);
    const time_t fields = timegm(&local);
#endif
    const int64_t unixTo2000 = 946684800;
    sCalendarBase = ((int64_t)fields - unixTo2000) * OS_TIMER_CLOCK;
}

OSTime OSGetTime(void) {
    return game_time() + sCalendarBase;
}

OSTick OSGetTick(void) {
    return (OSTick)(game_time() + sCalendarBase);
}

/* sPending: device interrupts in OS_INTERRUPTMASK's bit order (interrupt n is bit 31 - n), so the deliverable set is
   pending & ~mask, and the decrementer in bit 32, which no mask covers. Host threads set bits; the simulation thread
   clears them as it delivers. sIdle is set while the simulation thread sleeps for want of work: a raiser signals only
   then (both sides use sequentially consistent order, so one of them always sees the other). */

enum { kDecrementerShift = 32 };
#define DECREMENTER_PENDING (1ull << kDecrementerShift)

static _Atomic uint64_t sPending;
static atomic_int sIdle;

#if defined(_WIN32)
static HANDLE sWake;
#else
static pthread_mutex_t sWakeLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sWakeCond = PTHREAD_COND_INITIALIZER;
static int sWakePending;
#endif

static void wake_simulation(void) {
#if defined(_WIN32)
    SetEvent(sWake);
#else
    pthread_mutex_lock(&sWakeLock);
    sWakePending = 1;
    pthread_cond_signal(&sWakeCond);
    pthread_mutex_unlock(&sWakeLock);
#endif
}

static void raise_pending(uint64_t bits) {
    atomic_fetch_or(&sPending, bits);
    if (atomic_load(&sIdle)) {
        wake_simulation();
    }
}

void nebula_signal_simulation(void) {
    if (atomic_load(&sIdle)) {
        wake_simulation();
    }
}

/* Sleeps until something is raised (sIdle is set and the pending word re-checked by the caller first). */
static void sleep_for_work(void) {
#if defined(_WIN32)
    WaitForSingleObject(sWake, INFINITE);
#else
    pthread_mutex_lock(&sWakeLock);
    while (!sWakePending) {
        pthread_cond_wait(&sWakeCond, &sWakeLock);
    }
    sWakePending = 0;
    pthread_mutex_unlock(&sWakeLock);
#endif
}

/* The decrementer: a host thread that sleeps until the deadline PPCMtdec set and then raises its bit, as the
   console's decrementer interrupts the running thread. Its lateness is the host timer's (docs/NATIVE_PERFORMANCE.md);
   the game's alarms are timeouts, periodic speaker updates and sleeps, which that does not disturb. */

static _Atomic int64_t sDecrementerDeadline = INT64_MAX;
static _Atomic int64_t sClockArmed = INT64_MAX; /* the deadline the clock thread is sleeping toward */

/* The game's own clock reads (OSGetTime, OSGetTick; on the simulation thread) also catch a passed deadline, at no
   extra clock read: the alarm is then due at the next point interrupts are enabled instead of when the host timer
   wakes the clock thread. */
static int64_t game_time(void) {
    const int64_t now = nebula_time_base();
    int64_t deadline = atomic_load_explicit(&sDecrementerDeadline, memory_order_relaxed);
    if (__builtin_expect(now >= deadline, 0) && atomic_compare_exchange_strong(&sDecrementerDeadline, &deadline, INT64_MAX)) {
        atomic_fetch_or(&sPending, DECREMENTER_PENDING);
    }
    return now;
}

#if defined(_WIN32)
static HANDLE sClockWake;
static HANDLE sClockTimer;

static DWORD WINAPI clock_main(void* arg) {
    (void)arg;
    HANDLE handles[2] = {sClockWake, sClockTimer};
    for (;;) {
        int64_t deadline = atomic_load(&sDecrementerDeadline);
        atomic_store(&sClockArmed, deadline);
        if (atomic_load(&sDecrementerDeadline) != deadline) {
            continue;
        }
        if (deadline == INT64_MAX) {
            WaitForSingleObject(sClockWake, INFINITE);
            continue;
        }
        const int64_t now = nebula_time_base();
        if (now >= deadline) {
            if (atomic_compare_exchange_strong(&sDecrementerDeadline, &deadline, INT64_MAX)) {
                atomic_store(&sClockArmed, INT64_MAX);
                raise_pending(DECREMENTER_PENDING);
            }
            continue;
        }
        /* Due time in 100 ns units, negative = relative; rounded up so the wake is never early. */
        const int64_t ticks = deadline - now;
        LARGE_INTEGER due;
        due.QuadPart = -(LONGLONG)((ticks * 10000000 + OS_TIMER_CLOCK - 1) / OS_TIMER_CLOCK);
        SetWaitableTimer(sClockTimer, &due, 0, NULL, NULL, FALSE);
        WaitForMultipleObjects(2, handles, FALSE, INFINITE);
    }
}

static void clock_start(void) {
    sClockWake = CreateEventW(NULL, FALSE, FALSE, NULL);
    sClockTimer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (sClockTimer == NULL) {
        /* Windows before 10 1803 has no high-resolution timers: a normal one at 1 ms system timer resolution. */
        timeBeginPeriod(1);
        sClockTimer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS);
    }
    HANDLE thread = CreateThread(NULL, 64 * 1024, clock_main, NULL, 0, NULL);
    if (sClockWake == NULL || sClockTimer == NULL || thread == NULL) {
        nebula_fatal("could not start the decrementer's clock thread");
    }
    SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
    SetThreadDescription(thread, L"Nebula Decrementer");
    CloseHandle(thread);
}

static void clock_rearm(void) {
    SetEvent(sClockWake);
}
#else
static pthread_mutex_t sClockLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sClockCond;

static void* clock_main(void* arg) {
    (void)arg;
    pthread_mutex_lock(&sClockLock);
    for (;;) {
        int64_t deadline = atomic_load(&sDecrementerDeadline);
        atomic_store(&sClockArmed, deadline);
        if (deadline == INT64_MAX) {
            pthread_cond_wait(&sClockCond, &sClockLock);
            continue;
        }
        const int64_t now = nebula_time_base();
        if (now >= deadline) {
            if (atomic_compare_exchange_strong(&sDecrementerDeadline, &deadline, INT64_MAX)) {
                atomic_store(&sClockArmed, INT64_MAX);
                raise_pending(DECREMENTER_PENDING);
            }
            continue;
        }
        const int64_t ticks = deadline - now;
        const int64_t ns = (ticks / OS_TIMER_CLOCK) * 1000000000 + ((ticks % OS_TIMER_CLOCK) * 1000000000 + OS_TIMER_CLOCK - 1) / OS_TIMER_CLOCK;
#if defined(__APPLE__)
        struct timespec rel = {(time_t)(ns / 1000000000), (long)(ns % 1000000000)};
        pthread_cond_timedwait_relative_np(&sClockCond, &sClockLock, &rel);
#else
        struct timespec until;
        clock_gettime(CLOCK_MONOTONIC, &until);
        until.tv_sec += (time_t)(ns / 1000000000);
        until.tv_nsec += (long)(ns % 1000000000);
        if (until.tv_nsec >= 1000000000) {
            until.tv_sec++;
            until.tv_nsec -= 1000000000;
        }
        pthread_cond_timedwait(&sClockCond, &sClockLock, &until);
#endif
    }
    return NULL;
}

static void clock_start(void) {
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
#if !defined(__APPLE__)
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
    pthread_cond_init(&sClockCond, &attr);
    pthread_condattr_destroy(&attr);
    pthread_t thread;
    pthread_attr_t threadAttr;
    pthread_attr_init(&threadAttr);
    pthread_attr_setstacksize(&threadAttr, 64 * 1024);
    pthread_attr_setdetachstate(&threadAttr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&thread, &threadAttr, clock_main, NULL) != 0) {
        nebula_fatal("could not start the decrementer's clock thread");
    }
#if defined(__APPLE__)
    (void)thread;
#else
    pthread_setname_np(thread, "nebula-dec");
#endif
    pthread_attr_destroy(&threadAttr);
}

static void clock_rearm(void) {
    pthread_mutex_lock(&sClockLock);
    pthread_cond_signal(&sClockCond);
    pthread_mutex_unlock(&sClockLock);
}
#endif

static BOOL sInterruptsEnabled; /* the simulation thread's MSR[EE] */
static __OSExceptionHandler sExceptionHandlers[16];
static OSContext* sCurrentContext;

/* Device interrupts by the SDK's numbers (__OS_INTERRUPT_*): the SDK's own handlers register through
   __OSSetInterruptHandler as on the console, and host threads raise them (nebula_raise_interrupt). */
enum { kMaxInterrupts = 32 };
static __OSInterruptHandler sInterruptHandlers[kMaxInterrupts];
static OSInterruptMask sInterruptMask = 0xFFFFFFFFu; /* masked bits, OS_INTERRUPTMASK order */

/* The SDK dispatcher's priority (OSInterrupt.c, InterruptPrioTable): the first group with a deliverable interrupt
   wins, and within it the lowest interrupt number. */
static const OSInterruptMask kInterruptPriority[] = {0x100,     0x40,      0xF8000000, 0x200,     0x80,      0x10,
                                                     0x3000,    0x20,      0x3FF8C00,  0x4000000, 0x4000,    0xFFFFFFFF};

__OSInterruptHandler __OSSetInterruptHandler(__OSInterrupt interrupt, __OSInterruptHandler handler) {
    const __OSInterruptHandler previous = sInterruptHandlers[interrupt];
    sInterruptHandlers[interrupt] = handler;
    return previous;
}

__OSInterruptHandler __OSGetInterruptHandler(s32 interrupt) {
    return sInterruptHandlers[interrupt];
}

OSInterruptMask __OSMaskInterrupts(OSInterruptMask mask) {
    const OSInterruptMask previous = sInterruptMask;
    sInterruptMask |= mask;
    return previous;
}

/* An interrupt that was pending while masked is delivered as soon as it is unmasked with interrupts enabled. */
OSInterruptMask __OSUnmaskInterrupts(OSInterruptMask mask) {
    const BOOL enabled = OSDisableInterrupts();
    const OSInterruptMask previous = sInterruptMask;
    sInterruptMask &= ~mask;
    OSRestoreInterrupts(enabled);
    return previous;
}

void nebula_raise_interrupt(int interrupt) {
    raise_pending(OS_INTERRUPTMASK(interrupt));
}

/* Runs what is pending; called with interrupts enabled. A handler runs with them disabled, the scheduler disabled
   around it and the SDK's end-of-interrupt reschedule after it, which may switch threads; this resumes when the
   interrupted thread runs again. One handler per pass, as the console takes one exception at a time. A masked
   interrupt stays pending until unmasked. */
static void __attribute__((noinline)) deliver_interrupts(void) {
    for (;;) {
        const uint64_t deliverable = atomic_load_explicit(&sPending, memory_order_acquire) & ~(uint64_t)sInterruptMask;
        if (deliverable == 0) {
            return;
        }
        if (deliverable & DECREMENTER_PENDING) {
            atomic_fetch_and(&sPending, ~DECREMENTER_PENDING);
            const __OSExceptionHandler handler = sExceptionHandlers[__OS_EXCEPTION_DECREMENTER];
            if (handler != NULL) {
                sInterruptsEnabled = FALSE;
                handler(__OS_EXCEPTION_DECREMENTER, OSGetCurrentContext());
                sInterruptsEnabled = TRUE;
            }
            continue;
        }
        const OSInterruptMask devices = (OSInterruptMask)deliverable;
        const OSInterruptMask* group = kInterruptPriority;
        while ((devices & *group) == 0) {
            group++;
        }
        const int interrupt = __builtin_clz(devices & *group);
        atomic_fetch_and(&sPending, ~(uint64_t)OS_INTERRUPTMASK(interrupt));
        const __OSInterruptHandler handler = sInterruptHandlers[interrupt];
        if (handler != NULL) {
            sInterruptsEnabled = FALSE;
            OSDisableScheduler();
            handler((__OSInterrupt)interrupt, OSGetCurrentContext());
            OSEnableScheduler();
            __OSReschedule();
            sInterruptsEnabled = TRUE;
        }
    }
}

/* The hot path: one relaxed load and a test when nothing is pending (docs/NATIVE_PERFORMANCE.md). */
static inline BOOL work_pending(void) {
    return __builtin_expect((atomic_load_explicit(&sPending, memory_order_relaxed) & ~(uint64_t)sInterruptMask) != 0, 0);
}

static inline void enable_and_deliver(void) {
    sInterruptsEnabled = TRUE;
    if (work_pending()) {
        deliver_interrupts();
    }
}

/* Interrupts were disabled and are now enabled with work pending: the delivery, then the previous level (FALSE), as a
   tail call so the callers' common path needs no stack frame. */
static BOOL __attribute__((noinline)) deliver_from_disabled(void) {
    deliver_interrupts();
    return FALSE;
}

/* Work that arrived while interrupts were enabled is delivered before they are disabled: on the console it would have
   interrupted the code before this point, not run at the end of the critical section that follows. */
BOOL OSDisableInterrupts(void) {
    if (!sInterruptsEnabled) {
        return FALSE;
    }
    if (work_pending()) {
        deliver_interrupts();
    }
    sInterruptsEnabled = FALSE;
    return TRUE;
}

BOOL OSEnableInterrupts(void) {
    if (sInterruptsEnabled) {
        return TRUE;
    }
    sInterruptsEnabled = TRUE;
    return work_pending() ? deliver_from_disabled() : FALSE;
}

BOOL OSRestoreInterrupts(BOOL level) {
    const BOOL previous = sInterruptsEnabled;
    if (!level) {
        if (previous && work_pending()) {
            deliver_interrupts();
        }
        sInterruptsEnabled = FALSE;
        return previous;
    }
    if (previous) {
        return TRUE;
    }
    sInterruptsEnabled = TRUE;
    return work_pending() ? deliver_from_disabled() : FALSE;
}

/* The decrementer interrupts when it passes zero: PPCMtdec(n) makes it fire n ticks from now. The clock thread is
   woken only when the new deadline is earlier than the one it sleeps toward; for a later one it wakes at the old
   deadline, sees the new one and sleeps again. A decrementer interrupt already pending stays pending, as on the
   console; OSAlarm's handler re-arms when it finds nothing due. */
void PPCMtdec(u32 ticks) {
    const int64_t deadline = nebula_time_base() + (int64_t)ticks;
    atomic_store(&sDecrementerDeadline, deadline);
    if (deadline < atomic_load(&sClockArmed)) {
        clock_rearm();
    }
}

u32 PPCMfmsr(void) {
    return sInterruptsEnabled ? 0x8000u : 0u;
}

void PPCMtmsr(u32 msr) {
    OSRestoreInterrupts((msr & 0x8000u) != 0);
}

void PPCHalt(void) {
    nebula_fatal("PPCHalt");
}

__OSExceptionHandler __OSSetExceptionHandler(__OSException exception, __OSExceptionHandler handler) {
    const __OSExceptionHandler previous = sExceptionHandlers[exception];
    sExceptionHandlers[exception] = handler;
    return previous;
}

__OSExceptionHandler __OSGetExceptionHandler(__OSException exception) {
    return sExceptionHandlers[exception];
}

/* A thread spinning until another thread's work is done (a load-done flag, the displayed XFB): on the console the
   interrupt that completes the work, and the thread it wakes, preempt the spin. Natively the spin makes no OS call, so
   it polls here: what is pending is delivered (which may switch to the woken thread), and with nothing pending the
   simulation thread sleeps until a host thread raises something instead of spinning. The VI raises every retrace, so
   a wait on state a host thread writes without raising lasts at most one. */
void __NebulaPoll(void) {
    if (!sInterruptsEnabled) {
        return;
    }
    if (!work_pending()) {
        atomic_store(&sIdle, 1);
        if ((atomic_load(&sPending) & ~(uint64_t)sInterruptMask) == 0) {
            sleep_for_work();
        }
        atomic_store(&sIdle, 0);
    }
    if (work_pending()) {
        deliver_interrupts();
    }
}

/* No thread can run (SelectThread, interrupts disabled): deliver interrupts and sleep until one can. */
void __NebulaIdle(void) {
    while (RunQueueBits == 0) {
        sInterruptsEnabled = TRUE;
        deliver_interrupts();
        sInterruptsEnabled = FALSE;
        if (RunQueueBits != 0) {
            break;
        }
        atomic_store(&sIdle, 1);
        if ((atomic_load(&sPending) & ~(uint64_t)sInterruptMask) == 0) {
            sleep_for_work();
        }
        atomic_store(&sIdle, 0);
    }
}

/* The registers are the host's; what remains of OSContext is which one is current. */
void OSSetCurrentContext(OSContext* context) {
    sCurrentContext = context;
}

OSContext* OSGetCurrentContext(void) {
    return sCurrentContext;
}

void OSClearContext(OSContext* context) {
    context->mode = 0;
    context->state = 0;
}

void OSFillFPUContext(OSContext* context) {
    (void)context;
}

/* Its own frame, just below the caller's stack pointer; never inlined, so never the caller's frame. */
uintptr_t __attribute__((noinline)) OSGetStackPointer(void) {
    return (uintptr_t)__builtin_frame_address(0);
}

typedef struct {
    void* (*func)(void*);
    void* param;
} ThreadStart;

void nebula_thread_start(void (*entry)(void*), void* arg) {
    entry(arg);
    nebula_fatal("a thread's entry returned");
}

/* A new thread starts with interrupts enabled, as a new OSContext does, and ends in OSExitThread with its function's
   result. */
static void thread_main(void* arg) {
    NEBULA_ASAN_FINISH(NULL);
    const ThreadStart start = *(const ThreadStart*)arg;
    enable_and_deliver();
    OSExitThread(start.func(start.param));
    nebula_fatal("OSExitThread returned");
}

/* A diagnostic for the host's reports: every game thread's state and the addresses into the module's image found on
   its stack, newest first, as offsets from the image base (symbolize them with the module's PDB). The stack of the
   thread running now is read where it stands; a reading taken while the game runs is approximate. */
extern OSThreadQueue __OSActiveThreadQueue;

NEBULA_GAME_API NEBULA_NO_ASAN void nebula_game_debug_dump(void) { /* reads raw stacks */
    uintptr_t image, size;
    nebula_image_range(&image, &size);
    nebula_log("threads (current %p):\n", (void*)OSGetCurrentThread());
    for (OSThread* t = __OSActiveThreadQueue.head; t != NULL; t = t->linkActive.next) {
        char line[1536];
        int at = snprintf(line, sizeof(line), "  %p prio %d/%d state %u suspend %d queue %p mutex %p:", (void*)t, t->priority, t->base,
                          t->state, t->suspend, (void*)t->queue, (void*)t->mutex);
        const uintptr_t* sp = (const uintptr_t*)t->context.nativeSp;
        const uintptr_t* top = (const uintptr_t*)t->stackBase;
        int found = 0;
        for (int i = 0; sp != NULL && sp + i < top && i < 8192 && found < 48 && at < (int)sizeof(line) - 16; i++) {
            const uintptr_t v = sp[i];
            if (v - image < size) {
                at += snprintf(line + at, sizeof(line) - (size_t)at, " +%llx", (unsigned long long)(v - image));
                found++;
            }
        }
        /* the deepest the stack has been: its lowest nonzero word (stacks start zeroed) */
        const uintptr_t* low = (const uintptr_t*)(t->stackEnd + 2);
        while (low < top && *low == 0) {
            low++;
        }
        nebula_log("%s\n    stack %u of %u bytes used\n", line, (unsigned)((const u8*)top - (const u8*)low),
                   (unsigned)((const u8*)t->stackBase - (const u8*)t->stackEnd));
    }
}

void* __NebulaPrepareThread(void* (*func)(void*), void* param, void* stackTop, u32 stackSize) {
    uint8_t* top = (uint8_t*)((uintptr_t)stackTop & ~(uintptr_t)15) - 32;
    ThreadStart* start = (ThreadStart*)top;
    start->func = func;
    start->param = param;
    return nebula_prepare_stack(top, stackSize - 48, thread_main, start);
}

/* A game thread runs on a stack from the pool at the top of MEM2 (in the arena, where the GPU can read stack data the
   game gives GX), not on the buffer the game gives OSCreateThread: that is sized for the console's code, and x64
   frames are larger (an AddressSanitizer build's larger still, with exceptions dispatched on the current stack). A
   guard below each stack makes an overflow fault instead of overwriting the heap. A slot is reused once its thread has
   exited or been cancelled. */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NEBULA_STACK_MIN 0x100000u /* AddressSanitizer frames are several times larger */
#endif
#endif
#ifndef NEBULA_STACK_MIN
#define NEBULA_STACK_MIN 0x40000u /* 256 KB: the game's deepest stack, boot to the title screen, is 13.5 KB */
#endif
enum { kStackGuard = 0x4000u, kStackMin = NEBULA_STACK_MIN, kStackSlots = 64 };

typedef struct {
    OSThread* owner;
    uint8_t* end;
    u32 size;
} StackSlot;

static StackSlot sStackSlots[kStackSlots];
static int sStackSlotCount;
static uint8_t* sStackNext;

void* __NebulaThreadStack(OSThread* thread, u32 requested, u32* size) {
    const u32 want = (requested > kStackMin ? (requested + 0xFFFFu) & ~0xFFFFu : kStackMin);
    StackSlot* slot = NULL;
    for (int i = 0; i < sStackSlotCount; i++) {
        if (sStackSlots[i].owner == thread) {
            sStackSlots[i].owner = NULL; /* a thread object created again */
        }
    }
    for (int i = 0; i < sStackSlotCount && slot == NULL; i++) {
        if (sStackSlots[i].owner == NULL && sStackSlots[i].size >= want) {
            slot = &sStackSlots[i];
        }
    }
    if (slot == NULL) {
        size_t poolSize;
        uint8_t* pool = nebula_stack_pool(&poolSize);
        if (sStackNext == NULL) {
            sStackNext = pool;
        }
        if (sStackSlotCount == kStackSlots || sStackNext + kStackGuard + want > pool + poolSize) {
            nebula_fatal("no native stack left for a thread (%d stacks, %u bytes wanted)", sStackSlotCount, want);
        }
        nebula_memory_guard(sStackNext, kStackGuard);
        slot = &sStackSlots[sStackSlotCount++];
        slot->end = sStackNext + kStackGuard;
        slot->size = want;
        sStackNext += kStackGuard + want;
    }
    slot->owner = thread;
    *size = slot->size;
    return slot->end + slot->size;
}

/* An exiting thread still runs on its stack (the reschedule, the idle wait and the interrupts it delivers, which may
   create threads) until the switch away: its slot is freed by that switch, not here. */
static OSThread* sReleasing;

static void release_slot(const OSThread* thread) {
    for (int i = 0; i < sStackSlotCount; i++) {
        if (sStackSlots[i].owner == thread) {
            sStackSlots[i].owner = NULL;
        }
    }
}

void __NebulaReleaseThreadStack(OSThread* thread) {
    if (thread == (OSThread*)OSGetCurrentThread()) {
        if (sReleasing != NULL) {
            release_slot(sReleasing); /* an earlier exit, left behind: this thread runs now */
        }
        sReleasing = thread;
    } else {
        release_slot(thread);
    }
}

/* A thread that has come within 8 KB of its guard is reported once (an overflow into the guard leaves no stack to
   report from): the switching thread's stack pointer is its depth at this point. */
static void check_stack_depth(const OSThread* thread, const void* sp) {
    static int sWarned;
    if (thread->stackEnd != NULL && (const u8*)sp - (const u8*)thread->stackEnd < 0x2000 && !sWarned) {
        sWarned = 1;
        nebula_log("thread %p is within %d bytes of its stack's end\n", (const void*)thread,
                   (int)((const u8*)sp - (const u8*)thread->stackEnd));
    }
}

extern OSContext IdleContext; /* OSThread.c: the context of no thread, while the scheduler waits */

void __NebulaSwitchThread(OSContext* from, OSContext* to) {
    void* fake = NULL;
    const OSThread* next = (const OSThread*)to; /* the context is the thread's first member */
    const OSThread* self = from != &IdleContext ? (const OSThread*)from : NULL;
    if (self != NULL) {
        check_stack_depth(self, __builtin_frame_address(0));
    }
    NEBULA_ASAN_START(&fake, next->stackEnd, (size_t)((const u8*)next->stackBase - (const u8*)next->stackEnd));
    nebula_switch_stack(&from->nativeSp, to->nativeSp);
    NEBULA_ASAN_FINISH(fake);
    /* Running again (self), on another thread's switch: the exiting thread that switch left is off its stack now. */
    if (sReleasing != NULL && sReleasing != self) {
        release_slot(sReleasing);
        sReleasing = NULL;
    }
}

/* The boot thread runs on a stack in MEM1 below the arena, as the console's main stack is. */
enum { kBootStackTop = 0x00100000u, kBootStackSize = 0x00080000u };

void* __NebulaBootStackBase(void) {
    return nebula_arena_base() + kBootStackTop;
}

void* __NebulaBootStackEnd(void) {
    return nebula_arena_base() + kBootStackTop - kBootStackSize;
}

OSErrorHandler __OSErrorTable[17];
u32 __OSFpscrEnableBits = 0xF8;

OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler) {
    const OSErrorHandler previous = __OSErrorTable[error];
    __OSErrorTable[error] = handler;
    return previous;
}

static OSShutdownFunctionInfo* sShutdownFunctions;

void OSRegisterShutdownFunction(OSShutdownFunctionInfo* info) {
    info->next = sShutdownFunctions;
    sShutdownFunctions = info;
}

static OSPowerCallback sPowerCallback;

OSPowerCallback OSSetPowerCallback(OSPowerCallback callback) {
    const OSPowerCallback previous = sPowerCallback;
    sPowerCallback = callback;
    return previous;
}

BOOL OSGetResetButtonState(void) {
    return FALSE;
}

void OSRegisterVersion(const char* id) {
    (void)id;
}

void OSProtectRange(u32 channel, void* addr, u32 nBytes, u32 control) {
    (void)channel;
    (void)addr;
    (void)nBytes;
    (void)control;
}

/* Power: the host ends or restarts the game (the module never returns from these). */
static void power(u32 what) {
    const NebulaHostServices* host = nebula_host();
    host->power(host->user, what);
    exit(0);
}

void OSShutdownSystem(void) {
    power(NEBULA_POWER_SHUTDOWN);
}

void OSRestart(u32 resetCode) {
    (void)resetCode;
    power(NEBULA_POWER_RESTART);
}

void OSReturnToMenu(void) {
    power(NEBULA_POWER_MENU);
}

void OSRebootSystem(void) {
    power(NEBULA_POWER_REBOOT);
}

BOOL __DVDTestAlarm(const OSAlarm* alarm) {
    (void)alarm;
    return FALSE;
}

void OSReport(const char* format, ...) {
    va_list args;
    va_start(args, format);
    nebula_vlog(format, args);
    va_end(args);
}

void OSVReport(const char* format, va_list args) {
    nebula_vlog(format, args);
}

void OSPanic(const char* file, int line, const char* format, ...) {
    char message[768];
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    snprintf(text, sizeof(text), "the game stopped (OSPanic) at %s:%d: %s", file, line, message);
    fatal_text(text);
}

static void (*sGameMain)(void);
static void* sHostStack;

static void boot_thread(void* arg) {
    (void)arg;
    NEBULA_ASAN_FINISH(NULL);
    void *mem1Lo, *mem1Hi, *mem2Lo, *mem2Hi;
    nebula_memory_arenas(&mem1Lo, &mem1Hi, &mem2Lo, &mem2Hi);
    OSSetMEM1ArenaLo(mem1Lo);
    OSSetMEM1ArenaHi(mem1Hi);
    OSSetMEM2ArenaLo(mem2Lo);
    OSSetMEM2ArenaHi(mem2Hi);

    /* The system time's bias from the time base (__OSGetSystemTime): none, since the time base is already calendar
       time. */
    *(OSTime*)OSPhysicalToCached(0x30D8) = 0;

    sInterruptsEnabled = FALSE;
    __OSThreadInit();
    __OSInitAlarm();
    enable_and_deliver();
    sGameMain();
    nebula_fatal("the game's main returned");
}

void nebula_os_boot(void (*gameMain)(void)) {
#if defined(_WIN32)
    sWake = CreateEventW(NULL, FALSE, FALSE, NULL);
#endif
    time_init();
    calendar_init();
    clock_start();
    if (!nebula_memory_init()) {
        nebula_fatal("could not reserve the game's memory");
    }
    sGameMain = gameMain;
    void* sp = nebula_prepare_stack(__NebulaBootStackBase(), kBootStackSize, boot_thread, NULL);
    void* fake = NULL;
    NEBULA_ASAN_START(&fake, __NebulaBootStackEnd(), kBootStackSize);
    nebula_switch_stack(&sHostStack, sp);
    nebula_fatal("the boot thread returned to the host stack");
}
