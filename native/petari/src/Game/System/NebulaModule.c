/* The game module: its entry points (nebula_game_api.h), the host's services, the log, fatal errors and the boot. */
#include <nebula_game_api.h>

#include "nebula_module.h"

#include <stdarg.h>
#include <stdlib.h>

void nebula_game_main(void); /* Game/System/GameSystem.cpp: the game's main */

static const NebulaHostServices* sHost;

#if defined(_WIN32) && !defined(NEBULA_SANITIZE_BUILD)
void nebula_run_constructors(void);

void* malloc(size_t size) {
    return sHost->heap_alloc(sHost->user, size);
}

void* realloc(void* data, size_t size) {
    return sHost->heap_realloc(sHost->user, data, size);
}

void free(void* data) {
    sHost->heap_free(sHost->user, data);
}
#endif

const NebulaHostServices* nebula_host(void) {
    return sHost;
}

NEBULA_GAME_API uint32_t nebula_game_attach(const NebulaHostServices* host) {
    if (host == NULL || host->version < NEBULA_GAME_API_VERSION || host->size < sizeof(NebulaHostServices)) {
        return 0;
    }
    sHost = host;
#if defined(_WIN32) && !defined(NEBULA_SANITIZE_BUILD)
    nebula_run_constructors();
#endif
    return NEBULA_GAME_API_VERSION;
}

NEBULA_GAME_API void nebula_game_run(void) {
    if (sHost == NULL) {
        nebula_fatal("nebula_game_run before nebula_game_attach");
    }
    nebula_game_main();
    nebula_fatal("the game's main returned");
}

/* Each line goes to the host in one call, which writes it whole from any thread; nothing is shared between calls (dropped
   before the host attaches). Every caller ends its text with a newline; a last piece without one is a line of its own.
   Text of any length: up to 1023 characters on the stack, longer in the host's heap (never an arena, so an arena can log
   its own state). */
void nebula_vlog(const char* format, va_list args) {
    const NebulaHostServices* host = nebula_host();
    if (host == NULL || host->log == NULL) {
        return;
    }

    char stack_text[1024];
    va_list again;
    va_copy(again, args);
    const int n = nebula_vsnprintf(stack_text, sizeof(stack_text), format, args);
    char* text = stack_text;
    size_t length = n < 0 ? 0 : (size_t)n;
    if (length >= sizeof(stack_text)) {
        text = host->heap_alloc != NULL ? (char*)host->heap_alloc(host->user, length + 1) : NULL;
        if (text != NULL) {
            nebula_vsnprintf(text, length + 1, format, again);
        } else {
            text = stack_text;
            length = sizeof(stack_text) - 1;
        }
    }
    va_end(again);

    size_t start = 0;
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\n') {
            text[i] = '\0';
            host->log(host->user, text + start);
            start = i + 1;
        }
    }
    if (start < length) {
        text[length] = '\0';
        host->log(host->user, text + start);
    }
    if (text != stack_text) {
        host->heap_free(host->user, text);
    }
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
    (void)text;
    __builtin_trap();
}

void nebula_panic(const char* file, int line, const char* format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    nebula_vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    nebula_fatal("%s:%d: %s", file, line, message);
}

void nebula_fatal(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    nebula_vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    fatal_text(text);
}

/* The release the game is: North America (RMGE01), Europe (RMGP01) or Japan (RMGJ01), by its game id's fourth letter. */
char nebula_game_region(void) {
    return sHost->game_id(sHost->user)[3];
}
