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

void __NebulaGameContext(u32 what, s32 value) {
    sHost->game_context(sHost->user, what, value);
}

int __NebulaTestSceneFrame(void* action) {
    return sHost->test_scene_frame(sHost->user, (NebulaTestAction*)action);
}

/* The console's hardware registers, which some kept code still names (private/flipper.h): plain memory natively. */
vu16 __VIRegs[0x3B];
vu32 __PIRegs[0xC];
vu16 __DSPRegs[0x20];
vu32 __AIRegs[0x8];
vu32 __EXIRegs[0x10];
vu16 __MEMRegs[0x40];
vu32 __DIRegs[16];
vu32 __SIRegs[64];
vu32 __ACRRegs[89];
vu32 __IPCRegs[4];

void __NebulaSceneSurround(f32 intensity) {
    sHost->scene_surround(sHost->user, intensity);
}

u32 __NebulaOption(u32 what) {
    return sHost->option(sHost->user, what);
}

void __NebulaGameLight(u32 kind, u32 id, const f32* values) {
    sHost->game_light(sHost->user, kind, id, values);
}

void __NebulaGameStage(const char* name) {
    sHost->game_stage(sHost->user, name);
}

uint32_t __NebulaObjectId(const void* object); /* memory.c */

void __NebulaObjectName(const void* object, const char* name) {
    sHost->object_name(sHost->user, __NebulaObjectId(object), name);
}

f32 __NebulaLightReach(const char* model, f32 radius) {
    return sHost->light_reach(sHost->user, model, radius);
}

int __NebulaTracedSun(f32* direction) {
    return sHost->traced_sun(sHost->user, direction);
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

/* An SDK function the native layer does not implement yet (sdk/stubs). The game reaching one is a gap to fill,
   never something to step over. */
void nebula_sdk_missing(const char* name) {
    nebula_fatal("the game called %s, which the native SDK layer does not implement yet", name);
}
