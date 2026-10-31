/* The native SDK layer's core: the module's entry points (nebula_game_api.h) and what a missing piece does. */
#include <nebula_game_api.h>

#include "nebula_sdk.h"

void nebula_game_main(void); /* Game/System/GameSystem.cpp: the game's main */

static const NebulaHostServices* sHost;
/* The host's services as the game calls them: the material entries tell the game's material state which material
   the draws use (nebula_material_watch) before they reach the host. */
static NebulaHostServices sServices;
static void (*sMaterialWatch)(const NebulaNativeMaterial* native);

static void watch_material(void* user, uint64_t program, const NebulaMaterialValues* values) {
    if (sMaterialWatch != NULL) {
        sMaterialWatch(NULL);
    }
    sHost->material(user, program, values);
}

static void watch_native_material(void* user, const NebulaNativeMaterial* material) {
    if (sMaterialWatch != NULL) {
        sMaterialWatch(material);
    }
    sHost->native_material(user, material);
}

const NebulaHostServices* nebula_host(void) {
    return sHost != NULL ? &sServices : NULL;
}

void nebula_material_watch(void (*watch)(const NebulaNativeMaterial* native)) {
    sMaterialWatch = watch;
}

NEBULA_GAME_API uint32_t nebula_game_attach(const NebulaHostServices* host) {
    if (host == NULL || host->version < NEBULA_GAME_API_VERSION || host->size < sizeof(NebulaHostServices)) {
        return 0;
    }
    sHost = host;
    sServices = *host;
    sServices.material = watch_material;
    sServices.native_material = watch_native_material;
    return NEBULA_GAME_API_VERSION;
}

NEBULA_GAME_API void nebula_game_run(void) {
    if (sHost == NULL) {
        nebula_fatal("nebula_game_run before nebula_game_attach");
    }
    nebula_os_boot(nebula_game_main);
}

void __NebulaMovieFrame(const char* name, s32 frame, f32 frame_rate, const void* y, const void* u, const void* v,
                        u32 width, u32 height) {
    sHost->movie_frame(sHost->user, name, frame, frame_rate, y, u, v, width, height);
}

