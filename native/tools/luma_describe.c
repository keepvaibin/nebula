/* luma_describe MODULE OUT: loads the game module (no disc), attaches it with the few services describing needs and
   writes its type rows as JSON (nebula_game_luma_describe, contract types) to OUT, as the host's --luma-describe does.
   The game build runs it after the module links and compares OUT with the generator's (tools/lumacore/compare_types.py);
   exit 0 written, else 1. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nebula_game_api.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

/* The module's host as far as describing needs one: its heap (the module's malloc is the host's), its log and its
   fatal errors, to stderr. Every other service is null: the rows need no disc, no arena and no window. */
static void host_log(void* user, const char* text) {
    (void)user;
    fprintf(stderr, "%s\n", text);
}

static void host_fatal(void* user, const char* text) {
    (void)user;
    fprintf(stderr, "luma_describe: the game module stopped: %s\n", text);
    exit(1);
}

static void* host_alloc(void* user, size_t size) {
    (void)user;
    return malloc(size);
}

static void* host_realloc(void* user, void* data, size_t size) {
    (void)user;
    return realloc(data, size);
}

static void host_free(void* user, void* data) {
    (void)user;
    free(data);
}

static void write_piece(void* user, const char* text, uint32_t length) {
    FILE* const file = (FILE*)user;
    if (fwrite(text, 1, length, file) != length) {
        fprintf(stderr, "luma_describe: a write failed\n");
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: luma_describe <game module> <out.json>\n");
        return 1;
    }
#if defined(_WIN32)
    HMODULE module = LoadLibraryA(argv[1]);
    void* found = module != NULL ? (void*)GetProcAddress(module, "nebula_game_luma_describe") : NULL;
    void* attach_found = module != NULL ? (void*)GetProcAddress(module, "nebula_game_attach") : NULL;
#else
    void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    void* found = module != NULL ? dlsym(module, "nebula_game_luma_describe") : NULL;
    void* attach_found = module != NULL ? dlsym(module, "nebula_game_attach") : NULL;
#endif
    if (module == NULL || found == NULL || attach_found == NULL) {
        fprintf(stderr, "luma_describe: %s is no game module with type rows\n", argv[1]);
        return 1;
    }
    /* The module's memory and errors are the host's: attach first (its constructors run, as in the game). */
    static NebulaHostServices host;
    host.version = NEBULA_GAME_API_VERSION;
    host.size = sizeof(host);
    host.log = host_log;
    host.fatal = host_fatal;
    host.heap_alloc = host_alloc;
    host.heap_realloc = host_realloc;
    host.heap_free = host_free;
    uint32_t (*attach)(const NebulaHostServices*);
    memcpy(&attach, &attach_found, sizeof(attach));
    if (attach(&host) != NEBULA_GAME_API_VERSION) {
        fprintf(stderr, "luma_describe: %s did not attach (another game API version)\n", argv[1]);
        return 1;
    }
    FILE* const file = fopen(argv[2], "wb");
    if (file == NULL) {
        fprintf(stderr, "luma_describe: %s could not be opened\n", argv[2]);
        return 1;
    }
    void (*describe)(NebulaDescribeWrite, void*);
    memcpy(&describe, &found, sizeof(describe));
    describe(write_piece, file);
    const int failed = ferror(file) != 0;
    if (fclose(file) != 0 || failed) {
        fprintf(stderr, "luma_describe: %s could not be written\n", argv[2]);
        return 1;
    }
    return 0;
}
