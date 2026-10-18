/* The native OS core: what lies under the RVL SDK's own scheduler, alarms and synchronisation (OSThread.c,
   OSMutex.c, OSMessage.c, OSAlarm.c, OSTime.c, OSArena.c and OSAlloc.c, compiled as they are).

   All game threads run on one host thread, the simulation thread, one at a time, as on the console's single core:
   the SDK's SelectThread picks the thread, and a switch is a stack switch (context_switch.c). Interrupts (the
   decrementer, which drives OSAlarm, and device interrupts raised by host threads) are delivered on the
   simulation thread whenever the game enables them - at the end of every OS call that disabled them - with
   interrupts disabled during the handler, as the console runs its handlers. When no thread can run, the
   simulation thread sleeps until a host event instead of spinning. */
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <revolution/os.h>

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

extern volatile u32 RunQueueBits;
void __OSThreadInit(void);
void __OSInitAlarm(void);
void __OSReschedule(void);
void OSSetMEM1ArenaLo(void*);
void OSSetMEM1ArenaHi(void*);
void OSSetMEM2ArenaLo(void*);

/* Log */

void nebula_vlog(const char* format, va_list args) {
    vfprintf(stderr, format, args);
    fflush(stderr);
}

void nebula_log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    nebula_vlog(format, args);
    va_end(args);
}

void nebula_fatal(const char* format, ...) {
    va_list args;
    fputs("Nebula: fatal: ", stderr);
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
    abort();
}

/* Time base: 60.75 MHz (the console's bus clock / 4), counted from boot. */

#if defined(_WIN32)
static LARGE_INTEGER sClockStart, sClockFrequency;
#else
static struct timespec sClockStart;
#endif

static void time_init(void) {
#if defined(_WIN32)
    QueryPerformanceFrequency(&sClockFrequency);
    QueryPerformanceCounter(&sClockStart);
#else
    clock_gettime(CLOCK_MONOTONIC, &sClockStart);
#endif
}

/* count * OS_TIMER_CLOCK / rate without overflow (rate up to a few GHz): whole seconds and the remainder apart. */
static int64_t scale_to_time_base(int64_t count, int64_t rate) {
    return (count / rate) * (int64_t)OS_TIMER_CLOCK + (count % rate) * (int64_t)OS_TIMER_CLOCK / rate;
}

int64_t nebula_time_base(void) {
#if defined(_WIN32)
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return scale_to_time_base(now.QuadPart - sClockStart.QuadPart, sClockFrequency.QuadPart);
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    const int64_t ns = (int64_t)(now.tv_sec - sClockStart.tv_sec) * 1000000000 + (now.tv_nsec - sClockStart.tv_nsec);
    return scale_to_time_base(ns, 1000000000);
#endif
}

OSTime OSGetTime(void) {
    return nebula_time_base();
}

OSTick OSGetTick(void) {
    return (OSTick)nebula_time_base();
}

/* Waking the simulation thread */

#if defined(_WIN32)
static HANDLE sWake;
#else
static pthread_mutex_t sWakeLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sWakeCond = PTHREAD_COND_INITIALIZER;
static int sWakePending;
#endif

void nebula_signal_simulation(void) {
#if defined(_WIN32)
    SetEvent(sWake);
#else
    pthread_mutex_lock(&sWakeLock);
    sWakePending = 1;
    pthread_cond_signal(&sWakeCond);
    pthread_mutex_unlock(&sWakeLock);
#endif
}

/* Sleeps until a host event or `ticks` of the time base pass. */
static void wait_for_event(int64_t ticks) {
#if defined(_WIN32)
    DWORD ms = ticks <= 0 ? 0 : (DWORD)((ticks + OS_TIMER_CLOCK / 1000 - 1) / (OS_TIMER_CLOCK / 1000));
    WaitForSingleObject(sWake, ticks == INT64_MAX ? INFINITE : ms);
#else
    pthread_mutex_lock(&sWakeLock);
    if (!sWakePending) {
        if (ticks == INT64_MAX) {
            pthread_cond_wait(&sWakeCond, &sWakeLock);
        } else if (ticks > 0) {
            struct timespec until;
            clock_gettime(CLOCK_REALTIME, &until);
            const int64_t ns = (ticks / OS_TIMER_CLOCK) * 1000000000 + (ticks % OS_TIMER_CLOCK) * 1000000000 / OS_TIMER_CLOCK;
            until.tv_sec += ns / 1000000000;
            until.tv_nsec += ns % 1000000000;
            if (until.tv_nsec >= 1000000000) {
                until.tv_sec++;
                until.tv_nsec -= 1000000000;
            }
            pthread_cond_timedwait(&sWakeCond, &sWakeLock, &until);
        }
    }
    sWakePending = 0;
    pthread_mutex_unlock(&sWakeLock);
#endif
}

/* Interrupts */

static volatile BOOL sInterruptsEnabled;
static int64_t sDecrementerDeadline = INT64_MAX;
static __OSExceptionHandler sExceptionHandlers[16];
static OSContext* sCurrentContext;

enum { kMaxInterrupts = 32 };
static NebulaInterruptHandler sInterruptHandlers[kMaxInterrupts];
static int sInterruptCount;
static atomic_uint sPendingInterrupts;

int nebula_register_interrupt(NebulaInterruptHandler handler) {
    if (sInterruptCount == kMaxInterrupts) {
        nebula_fatal("nebula_register_interrupt: more than %d interrupt sources", kMaxInterrupts);
    }
    sInterruptHandlers[sInterruptCount] = handler;
    return sInterruptCount++;
}

void nebula_raise_interrupt(int id) {
    atomic_fetch_or(&sPendingInterrupts, 1u << id);
    nebula_signal_simulation();
}

/* Runs what is pending; called with interrupts enabled. A handler runs with them disabled and may switch threads
   at its end (the SDK's end-of-interrupt reschedule); this resumes when the interrupted thread runs again. */
static void deliver_interrupts(void) {
    for (;;) {
        if (nebula_time_base() >= sDecrementerDeadline) {
            __OSExceptionHandler handler = sExceptionHandlers[__OS_EXCEPTION_DECREMENTER];
            sDecrementerDeadline = INT64_MAX;
            if (handler != NULL) {
                sInterruptsEnabled = FALSE;
                handler(__OS_EXCEPTION_DECREMENTER, OSGetCurrentContext());
                sInterruptsEnabled = TRUE;
            }
            continue;
        }
        const unsigned pending = atomic_exchange(&sPendingInterrupts, 0u);
        if (pending == 0) {
            return;
        }
        for (int id = 0; id < sInterruptCount; id++) {
            if (pending & (1u << id)) {
                sInterruptsEnabled = FALSE;
                OSDisableScheduler();
                sInterruptHandlers[id]();
                OSEnableScheduler();
                __OSReschedule();
                sInterruptsEnabled = TRUE;
            }
        }
    }
}

BOOL OSDisableInterrupts(void) {
    const BOOL previous = sInterruptsEnabled;
    sInterruptsEnabled = FALSE;
    return previous;
}

BOOL OSEnableInterrupts(void) {
    const BOOL previous = sInterruptsEnabled;
    sInterruptsEnabled = TRUE;
    if (!previous) {
        deliver_interrupts();
    }
    return previous;
}

BOOL OSRestoreInterrupts(BOOL level) {
    const BOOL previous = sInterruptsEnabled;
    sInterruptsEnabled = level ? TRUE : FALSE;
    if (level && !previous) {
        deliver_interrupts();
    }
    return previous;
}

/* The decrementer interrupts when it passes zero: PPCMtdec(n) makes it fire n ticks from now. */
void PPCMtdec(u32 ticks) {
    sDecrementerDeadline = nebula_time_base() + (int64_t)ticks;
    nebula_signal_simulation();
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

/* No thread can run (SelectThread, interrupts disabled): deliver interrupts and sleep until one can. */
void __NebulaIdle(void) {
    while (RunQueueBits == 0) {
        sInterruptsEnabled = TRUE;
        deliver_interrupts();
        sInterruptsEnabled = FALSE;
        if (RunQueueBits != 0) {
            break;
        }
        const int64_t deadline = sDecrementerDeadline;
        wait_for_event(deadline == INT64_MAX ? INT64_MAX : deadline - nebula_time_base());
    }
}

/* Contexts: the registers are the host's; what remains of OSContext is which one is current. */

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

uintptr_t OSGetStackPointer(void) {
    return (uintptr_t)__builtin_frame_address(0);
}

/* Threads */

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
    const ThreadStart start = *(const ThreadStart*)arg;
    sInterruptsEnabled = TRUE;
    deliver_interrupts();
    OSExitThread(start.func(start.param));
    nebula_fatal("OSExitThread returned");
}

void* __NebulaPrepareThread(void* (*func)(void*), void* param, void* stackTop, u32 stackSize) {
    uint8_t* top = (uint8_t*)((uintptr_t)stackTop & ~(uintptr_t)15) - 32;
    ThreadStart* start = (ThreadStart*)top;
    start->func = func;
    start->param = param;
    return nebula_prepare_stack(top, stackSize - 48, thread_main, start);
}

void __NebulaSwitchThread(OSContext* from, OSContext* to) {
    nebula_switch_stack(&from->nativeSp, to->nativeSp);
}

/* The boot thread runs on a stack in MEM1 below the arena, as the console's main stack is. */
enum { kBootStackTop = 0x00100000u, kBootStackSize = 0x00080000u };

void* __NebulaBootStackBase(void) {
    return nebula_arena_base() + kBootStackTop;
}

void* __NebulaBootStackEnd(void) {
    return nebula_arena_base() + kBootStackTop - kBootStackSize;
}

/* Error handlers, shutdown functions, power and reset */

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

void OSShutdownSystem(void) {
    exit(0);
}

void OSRestart(u32 resetCode) {
    (void)resetCode;
    exit(0);
}

void OSReturnToMenu(void) {
    exit(0);
}

void OSRebootSystem(void) {
    exit(0);
}

BOOL __DVDTestAlarm(const OSAlarm* alarm) {
    (void)alarm;
    return FALSE;
}

/* Reports */

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
    va_list args;
    fprintf(stderr, "Nebula: the game stopped (OSPanic) at %s:%d: ", file, line);
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    fflush(stderr);
    abort();
}

/* Boot */

static void (*sGameMain)(void);
static void* sHostStack;

static void boot_thread(void* arg) {
    (void)arg;
    void *mem1Lo, *mem1Hi, *mem2Lo, *mem2Hi;
    nebula_memory_arenas(&mem1Lo, &mem1Hi, &mem2Lo, &mem2Hi);
    OSSetMEM1ArenaLo(mem1Lo);
    OSSetMEM1ArenaHi(mem1Hi);
    OSSetMEM2ArenaLo(mem2Lo);
    OSSetMEM2ArenaHi(mem2Hi);

    /* The offset from the time base to calendar time (seconds since 2000-01-01) in low memory. */
    {
        const int64_t unixTo2000 = 946684800;
        const int64_t now = (int64_t)time(NULL) - unixTo2000;
        *(OSTime*)OSPhysicalToCached(0x30D8) = now * OS_TIMER_CLOCK - nebula_time_base();
    }

    sInterruptsEnabled = FALSE;
    __OSThreadInit();
    __OSInitAlarm();
    sInterruptsEnabled = TRUE;
    sGameMain();
    nebula_fatal("the game's main returned");
}

void nebula_os_boot(void (*gameMain)(void)) {
#if defined(_WIN32)
    timeBeginPeriod(1);
    sWake = CreateEventW(NULL, FALSE, FALSE, NULL);
#endif
    time_init();
    if (!nebula_memory_init()) {
        nebula_fatal("could not reserve the game's memory");
    }
    sGameMain = gameMain;
    void* sp = nebula_prepare_stack(__NebulaBootStackBase(), kBootStackSize, boot_thread, NULL);
    nebula_switch_stack(&sHostStack, sp);
    nebula_fatal("the boot thread returned to the host stack");
}
