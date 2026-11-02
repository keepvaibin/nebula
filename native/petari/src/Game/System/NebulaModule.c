/* The game module: its entry points (nebula_game_api.h), the host's services, the log, fatal errors and the boot. */
#include <nebula_game_api.h>

#include "nebula_module.h"
#include "nebula_threads.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

void nebula_game_main(void); /* Game/System/GameSystem.cpp: the game's main */

static const NebulaHostServices* sHost;

const NebulaHostServices* nebula_host(void) {
    return sHost;
}

NEBULA_GAME_API uint32_t nebula_game_attach(const NebulaHostServices* host) {
    if (host == NULL || host->version < NEBULA_GAME_API_VERSION || host->size < sizeof(NebulaHostServices)) {
        return 0;
    }
    sHost = host;
    return NEBULA_GAME_API_VERSION;
}

NEBULA_GAME_API void nebula_game_run(void) {
    if (sHost == NULL) {
        nebula_fatal("nebula_game_run before nebula_game_attach");
    }
    if (!nebula_memory_init()) {
        nebula_fatal("could not reserve the game's memory");
    }
    nebula_threads_init();
    nebula_game_main();
    nebula_fatal("the game's main returned");
}

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

/* The release the game is: North America (RMGE01), Europe (RMGP01) or Japan (RMGJ01), by its game id's fourth letter. */
char nebula_game_region(void) {
    return sHost->game_id(sHost->user)[3];
}
