/* The platform under the game's threads (Game/System/GameThreads): host threads and their sleep, the kernel's two locks, the log
   and fatal errors, and the boot. */
#include <stdarg.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <revolution/os.h>

#include <nebula_game_api.h>

#include "nebula_threads.h"
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

NEBULA_GAME_API void nebula_game_debug_dump(void) {
    nebula_threads_dump();
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

#if defined(_WIN32)
static SRWLOCK sLocks[2] = {SRWLOCK_INIT, SRWLOCK_INIT};

void nebula_host_lock(int which) {
    AcquireSRWLockExclusive(&sLocks[which]);
}

void nebula_host_unlock(int which) {
    ReleaseSRWLockExclusive(&sLocks[which]);
}
#else
static pthread_mutex_t sLocks[2] = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_MUTEX_INITIALIZER};

void nebula_host_lock(int which) {
    pthread_mutex_lock(&sLocks[which]);
}

void nebula_host_unlock(int which) {
    pthread_mutex_unlock(&sLocks[which]);
}
#endif

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
    if (!nebula_memory_init()) {
        nebula_fatal("could not reserve the game's memory");
    }
    nebula_threads_init();
    gameMain();
    nebula_fatal("the game's main returned");
}
