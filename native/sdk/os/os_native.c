/* The platform under the game's thread kernel (Game/System/NebulaKernel): the time base, the calendar, the timer thread that
   raises the kernel's timer event, the simulation thread's sleep and wake, the game threads' native stacks, the log
   and fatal errors, and the boot onto the boot thread. The kernel schedules; this layer provides what is
   platform-specific. */
#include <stdarg.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <revolution/os.h>

#include <nebula_game_api.h>

#include "nebula_kernel.h"
#include "nebula_sdk.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <errno.h>
#include <pthread.h>
#include <time.h>
#endif

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

/* A clock the kernel sets (nebula_time_set: deterministic test mode); -1 while the host's clock runs. */
static _Atomic int64_t sSetTime = -1;
void nebula_time_set(int64_t time) { sSetTime = time; }

int64_t nebula_time_base(void) {
    if (sSetTime >= 0) return sSetTime;
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

/* The simulation thread's sleep for want of work and a host thread's wake of it (the kernel decides when). */
#if defined(_WIN32)
static HANDLE sWake;
#else
static pthread_mutex_t sWakeLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sWakeCond = PTHREAD_COND_INITIALIZER;
static int sWakePending;
#endif

void nebula_wake_simulation(void) {
#if defined(_WIN32)
    SetEvent(sWake);
#else
    pthread_mutex_lock(&sWakeLock);
    sWakePending = 1;
    pthread_cond_signal(&sWakeCond);
    pthread_mutex_unlock(&sWakeLock);
#endif
}

void nebula_sleep_for_work(void) {
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

/* The timer: a host thread that sleeps until the deadline the kernel armed (nebula_timer_arm) and then raises the
   kernel's timer event. Its lateness is the host timer's (docs/NATIVE_PERFORMANCE.md); the game's alarms are
   timeouts, periodic speaker updates and sleeps, which that does not disturb. */

static _Atomic int64_t sTimerDeadline = INT64_MAX; /* in time-base ticks since boot */
static _Atomic int64_t sClockArmed = INT64_MAX; /* the deadline the clock thread is sleeping toward */

/* The kernel's own clock reads (nebula_time, on the simulation thread) also catch a passed deadline, at no extra
   clock read: the alarm is then due at the next scheduling point instead of when the host timer wakes the timer
   thread. */
static int64_t game_time(void) {
    const int64_t now = nebula_time_base();
    int64_t deadline = atomic_load_explicit(&sTimerDeadline, memory_order_relaxed);
    if (__builtin_expect(now >= deadline, 0) && atomic_compare_exchange_strong(&sTimerDeadline, &deadline, INT64_MAX)) {
        nebula_note_event(NEBULA_EVENT_TIMER);
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
        int64_t deadline = atomic_load(&sTimerDeadline);
        atomic_store(&sClockArmed, deadline);
        if (atomic_load(&sTimerDeadline) != deadline) {
            continue;
        }
        if (deadline == INT64_MAX) {
            WaitForSingleObject(sClockWake, INFINITE);
            continue;
        }
        const int64_t now = nebula_time_base();
        if (now >= deadline) {
            if (atomic_compare_exchange_strong(&sTimerDeadline, &deadline, INT64_MAX)) {
                atomic_store(&sClockArmed, INT64_MAX);
                nebula_raise_event(NEBULA_EVENT_TIMER);
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
        nebula_fatal("could not start the timer thread");
    }
    SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
    SetThreadDescription(thread, L"Nebula Timer");
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
        int64_t deadline = atomic_load(&sTimerDeadline);
        atomic_store(&sClockArmed, deadline);
        if (deadline == INT64_MAX) {
            pthread_cond_wait(&sClockCond, &sClockLock);
            continue;
        }
        const int64_t now = nebula_time_base();
        if (now >= deadline) {
            if (atomic_compare_exchange_strong(&sTimerDeadline, &deadline, INT64_MAX)) {
                atomic_store(&sClockArmed, INT64_MAX);
                nebula_raise_event(NEBULA_EVENT_TIMER);
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
        nebula_fatal("could not start the timer thread");
    }
#if defined(__APPLE__)
    (void)thread;
#else
    pthread_setname_np(thread, "nebula-timer");
#endif
    pthread_attr_destroy(&threadAttr);
}

static void clock_rearm(void) {
    pthread_mutex_lock(&sClockLock);
    pthread_cond_signal(&sClockCond);
    pthread_mutex_unlock(&sClockLock);
}
#endif

/* The kernel's time, ticks since boot (dates are the host's, NebulaCalendar): alarms are set in it. */
int64_t nebula_time(void) {
    return game_time();
}

/* The timer thread is woken only when the new deadline is earlier than the one it sleeps toward; for a later one it
   wakes at the old deadline, sees the new one and sleeps again. A timer event already raised stays raised; the
   kernel re-arms when it finds nothing due. */
void nebula_timer_arm(int64_t deadline) {
    atomic_store(&sTimerDeadline, deadline);
    if (sSetTime < 0 && deadline < atomic_load(&sClockArmed)) {
        clock_rearm();
    }
}

NEBULA_GAME_API void nebula_game_debug_dump(void) {
    nebula_kernel_dump();
}

/* The game's host threads: each sleeps on its own turn; only the one whose turn it is runs. A thread's own stack, at least
   this size: the game's numbers are the console's, and x64 frames are larger (AddressSanitizer's several times so). */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NEBULA_STACK_MIN 0x100000u
#endif
#endif
#ifndef NEBULA_STACK_MIN
#define NEBULA_STACK_MIN 0x40000u /* 256 KB: the game's deepest stack, boot to the title screen, is 13.5 KB */
#endif

struct NebulaHostThread {
    void (*entry)(void*);
    void* arg;
    bool cancelled;
#if defined(_WIN32)
    HANDLE turn;
#else
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool turn;
#endif
};

static NebulaHostThread* host_thread_new(void (*entry)(void*), void* arg) {
    NebulaHostThread* thread = (NebulaHostThread*)calloc(1, sizeof(NebulaHostThread));
    if (thread == NULL) {
        nebula_fatal("could not allocate a thread");
    }
    thread->entry = entry;
    thread->arg = arg;
#if defined(_WIN32)
    thread->turn = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (thread->turn == NULL) {
        nebula_fatal("could not create a thread's event");
    }
#else
    pthread_mutex_init(&thread->lock, NULL);
    pthread_cond_init(&thread->cond, NULL);
#endif
    return thread;
}

void nebula_host_thread_pass(NebulaHostThread* pTo) {
#if defined(_WIN32)
    SetEvent(pTo->turn);
#else
    pthread_mutex_lock(&pTo->lock);
    pTo->turn = true;
    pthread_cond_signal(&pTo->cond);
    pthread_mutex_unlock(&pTo->lock);
#endif
}

void nebula_host_thread_exit(NebulaHostThread* pSelf) {
#if defined(_WIN32)
    CloseHandle(pSelf->turn);
    free(pSelf);
    ExitThread(0);
#else
    pthread_mutex_destroy(&pSelf->lock);
    pthread_cond_destroy(&pSelf->cond);
    free(pSelf);
    pthread_exit(NULL);
#endif
}

void nebula_host_thread_wait(NebulaHostThread* pSelf) {
#if defined(_WIN32)
    WaitForSingleObject(pSelf->turn, INFINITE);
#else
    pthread_mutex_lock(&pSelf->lock);
    while (!pSelf->turn) {
        pthread_cond_wait(&pSelf->cond, &pSelf->lock);
    }
    pSelf->turn = false;
    pthread_mutex_unlock(&pSelf->lock);
#endif
    if (pSelf->cancelled) {
        nebula_host_thread_exit(pSelf);
    }
}

void nebula_host_thread_cancel(NebulaHostThread* pThread) {
    pThread->cancelled = true;
    nebula_host_thread_pass(pThread);
}

NebulaHostThread* nebula_host_thread_adopt(void) {
    return host_thread_new(NULL, NULL);
}

static void host_thread_run(NebulaHostThread* pSelf) {
    nebula_host_thread_wait(pSelf);
    pSelf->entry(pSelf->arg);
    nebula_fatal("a thread's entry returned");
}

#if defined(_WIN32)
static DWORD WINAPI host_thread_main(void* arg) {
    host_thread_run((NebulaHostThread*)arg);
    return 0;
}
#else
static void* host_thread_main(void* arg) {
    host_thread_run((NebulaHostThread*)arg);
    return NULL;
}
#endif

NebulaHostThread* nebula_host_thread_create(void (*entry)(void*), void* arg, uint32_t stackSize) {
    NebulaHostThread* thread = host_thread_new(entry, arg);
    const uint32_t size = stackSize > NEBULA_STACK_MIN ? stackSize : NEBULA_STACK_MIN;
#if defined(_WIN32)
    HANDLE handle = CreateThread(NULL, size, host_thread_main, thread, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (handle == NULL) {
        nebula_fatal("could not start a thread");
    }
    SetThreadDescription(handle, L"Nebula Game Thread");
    CloseHandle(handle);
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, size);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t handle;
    if (pthread_create(&handle, &attr, host_thread_main, thread) != 0) {
        nebula_fatal("could not start a thread");
    }
    pthread_attr_destroy(&attr);
#endif
    return thread;
}

void nebula_os_boot(void (*gameMain)(void)) {
#if defined(_WIN32)
    sWake = CreateEventW(NULL, FALSE, FALSE, NULL);
#endif
    time_init();
    if (nebula_host() == NULL || nebula_host()->test_frame_step == NULL) {
        clock_start();
    }
    if (!nebula_memory_init()) {
        nebula_fatal("could not reserve the game's memory");
    }
    nebula_kernel_init();
    nebula_interrupts_restore(1);
    gameMain();
    nebula_fatal("the game's main returned");
}
