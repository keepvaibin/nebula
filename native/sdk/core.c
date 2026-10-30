/* The native SDK layer's core: the module's entry points (nebula_game_api.h) and what a missing piece does. */
#include <nebula_game_api.h>

#include "nebula_sdk.h"

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
    nebula_load_dol_data();
    nebula_os_boot(nebula_game_main);
}

void __NebulaMovieFrame(const char* name, s32 frame, f32 frame_rate, const void* y, const void* u, const void* v,
                        u32 width, u32 height) {
    sHost->movie_frame(sHost->user, name, frame, frame_rate, y, u, v, width, height);
}

NEBULA_GAME_API int nebula_game_memory_regions(NebulaGameMemoryRegion* out, int max) {
    NebulaMemoryRegion regions[4];
    int count = 0;
    nebula_memory_regions(regions, &count);
    for (int i = 0; i < count && i < max; i++) {
        out[i].physical = regions[i].physical;
        out[i].size = regions[i].size;
        out[i].host = regions[i].host;
    }
    return count;
}
