/* The renderer's markers from the game: draw identities, bloom phases and clear quads reach the host as records in
   the order the game makes them (the game writes no command stream). */
#ifndef NEBULA_GX_FIFO_H
#define NEBULA_GX_FIFO_H

#include <stdint.h>

#include <nebula_game_api.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Nonzero while the renderer reads draw identities (frame interpolation, motion vectors, ray tracing; asked at each
   scene's begin). */
extern uint8_t __NebulaGXMarkers;

const struct NebulaHostServices* nebula_host(void);

/* A host marker (galaxy/motion_signals.h) to the renderer, after every command written so far. */
static inline void __NebulaGXMarker(uint8_t kind, uint16_t aux, uint32_t a, uint32_t b) {
    const struct NebulaHostServices* const host = nebula_host();
    host->marker(host->user, kind, aux, a, b);
}

/* A host marker: the game object whose drawing follows, or a scene signal. Never inside a GXBegin. */
static inline void __NebulaGXHostMarker(uint8_t kind, uint16_t aux, uint32_t a, uint32_t b) {
    if (__NebulaGXMarkers != 0) {
        __NebulaGXMarker(kind, aux, a, b);
    }
}

/* The game's bloom through the renderer (NEBULA_OPTION_BLOOM; galaxy/motion_signals.h kMarkerBloom, its phases and
   values): a host marker whatever the draw identities. */
enum {
    NEBULA_GX_MARKER_BLOOM = 15,
    NEBULA_GX_BLOOM_KEEP = 1,
    NEBULA_GX_BLOOM_NORMAL = 2,
    NEBULA_GX_BLOOM_SIMPLE = 3,
    NEBULA_GX_BLOOM_RESTORE = 4,
};

static inline void __NebulaGXBloomMarker(uint16_t phase, uint32_t a, uint32_t b) {
    __NebulaGXMarker(NEBULA_GX_MARKER_BLOOM, phase, a, b);
}

/* The next draw is an EFB clear quad (MainLoopFramework::clearEfb, MR::clearZBuffer): one quad filling its viewport
   in a constant colour, writing its Z texture's depth (`z`, 24 bits) with the compare always. The renderer clears that
   depth and draws the colour without the Z texture where the draw is the one it expects; else the quad draws as
   written. With its colour (__NebulaGXClearQuadColourMarker: the TEV's only stage outputs C0 and A0, `rgba`) the
   renderer clears the colour too. */
enum { NEBULA_GX_MARKER_CLEAR_QUAD = 16 };

static inline void __NebulaGXClearQuadMarkerWith(uint32_t z, uint16_t has_colour, uint32_t rgba) {
    __NebulaGXMarker(NEBULA_GX_MARKER_CLEAR_QUAD, has_colour, z, rgba);
}

static inline void __NebulaGXClearQuadMarker(uint32_t z) {
    __NebulaGXClearQuadMarkerWith(z, 0, 0);
}

static inline void __NebulaGXClearQuadColourMarker(uint32_t z, uint32_t rgba) {
    __NebulaGXClearQuadMarkerWith(z, 1, rgba);
}

#ifdef __cplusplus
}

#endif

#endif
