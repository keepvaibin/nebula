/* luma_describe [--services] MODULE OUT: loads the game module (no disc), attaches it with the few services describing
   needs and writes its type rows as JSON (nebula_game_luma_describe, contract types) to OUT, as the host's
   --luma-describe does; with --services the services catalog (luma_services_describe, luma_service_provider.h), as the
   host's --luma-services does. The game build runs it after the module links: it compares the rows with the
   generator's (tools/lumacore/compare_types.py) and writes the catalog as the names folder's luma_services.json
   (checked by tools/lumacore/check_services.py). Exit 0 written, else 1. */
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
    const int services = argc == 4 && strcmp(argv[1], "--services") == 0;
    if (argc != 3 && !services) {
        fprintf(stderr, "usage: luma_describe [--services] <game module> <out.json>\n");
        return 1;
    }
    const char* const module_path = argv[services ? 2 : 1];
    const char* const out_path = argv[services ? 3 : 2];
    const char* const export_name = services ? "luma_services_describe" : "nebula_game_luma_describe";
#if defined(_WIN32)
    HMODULE module = LoadLibraryA(module_path);
    void* found = module != NULL ? (void*)GetProcAddress(module, export_name) : NULL;
    void* attach_found = module != NULL ? (void*)GetProcAddress(module, "nebula_game_attach") : NULL;
#else
    void* module = dlopen(module_path, RTLD_NOW | RTLD_LOCAL);
    void* found = module != NULL ? dlsym(module, export_name) : NULL;
    void* attach_found = module != NULL ? dlsym(module, "nebula_game_attach") : NULL;
#endif
    if (module == NULL || found == NULL || attach_found == NULL) {
        fprintf(stderr, "luma_describe: %s is no game module with %s\n", module_path, export_name);
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
        fprintf(stderr, "luma_describe: %s did not attach (another game API version)\n", module_path);
        return 1;
    }
    FILE* const file = fopen(out_path, "wb");
    if (file == NULL) {
        fprintf(stderr, "luma_describe: %s could not be opened\n", out_path);
        return 1;
    }
    void (*describe)(NebulaDescribeWrite, void*);
    memcpy(&describe, &found, sizeof(describe));
    describe(write_piece, file);
    const int failed = ferror(file) != 0;
    if (fclose(file) != 0 || failed) {
        fprintf(stderr, "luma_describe: %s could not be written\n", out_path);
        return 1;
    }
    return 0;
}
