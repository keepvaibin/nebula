/* The renderer's markers from the game (nebula_gx_fifo.h): draw identities, bloom phases and clear quads as records,
   scene signals and stripe particles. The game writes no command stream: its drawing reaches the host as records. */
#include <revolution/os.h>

#include <nebula_game_api.h>
#include <nebula_gx_fifo.h>

#include "nebula_sdk.h"

const NebulaHostServices* nebula_host(void);

/* The writer position records name (always the start: no command is written). */
NebulaGXWriter __NebulaGXWriter;

/* Nonzero while the renderer reads draw identities (asked at each scene's begin). */
uint8_t __NebulaGXMarkers;

void __NebulaGXSceneMarker(u32 marker, const f32 (*view)[4]) {
    if (marker == NEBULA_SCENE_3D_BEGIN || marker == NEBULA_SCENE_2D_BEGIN) {
        __NebulaGXMarkers = (uint8_t)(nebula_host()->option(nebula_host()->user, NEBULA_OPTION_DRAW_IDENTITIES) != 0);
    }
    nebula_host()->gx_scene_marker(nebula_host()->user, marker, view != NULL ? &view[0][0] : NULL);
}

void __NebulaGXStripeParticle(u32 particle, u32 ageLifetime, u8 strip) {
    if (__NebulaGXMarkers != 0) {
        nebula_host()->gx_stripe_particle(nebula_host()->user, particle, ageLifetime, strip);
    }
}
