#pragma once

// The renderer's signals from the game (galaxy/motion_signals.h): host markers (the game object whose drawing
// follows, scene signals), bloom phases, EFB clear quads, scene bounds and JPA stripe particles. Each reaches the
// host as a record in the order the game makes them, between its drawing records.

#include <stdint.h>

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace NebulaRender {
    // Nonzero while the renderer reads draw identities (frame interpolation, motion vectors, ray tracing): asked at
    // each scene's begin (sceneMarker).
    extern uint8_t sDrawIdentities;

    // The game rewrote [p, p + size) (a decompressed archive over memory that may have held an image): an image handed
    // over there must be handed over again.
    inline void memoryRewritten(const void* p, uint32_t size) {
        const NebulaHostServices* const host = nebula_host();
        host->gpu_coherent(host->user, p, size);
    }

    // A host marker, whatever the draw identities.
    inline void marker(uint8_t kind, uint16_t aux, uint64_t a, uint64_t b) {
        const NebulaHostServices* const host = nebula_host();
        host->marker(host->user, kind, aux, a, b);
    }

    // A host marker while the renderer reads draw identities: the game object whose drawing follows, or a scene
    // signal.
    inline void hostMarker(uint8_t kind, uint16_t aux, uint64_t a, uint64_t b) {
        if (sDrawIdentities != 0) {
            marker(kind, aux, a, b);
        }
    }

    // The game's bloom through the renderer (NEBULA_OPTION_BLOOM; galaxy/motion_signals.h kMarkerBloom, its phases
    // and values).
    enum {
        kMarkerBloom = 15,
        kBloomKeep = 1,
        kBloomNormal = 2,
        kBloomSimple = 3,
        kBloomRestore = 4,
    };

    inline void bloomMarker(uint16_t phase, uint32_t a, uint32_t b) {
        marker(kMarkerBloom, phase, a, b);
    }

    // The next draw is an EFB clear quad (MainLoopFramework::clearEfb, MR::clearZBuffer): one quad filling its
    // viewport in a constant colour, writing its Z texture's depth (`z`, 24 bits) with the compare always. The renderer
    // clears that depth and draws the colour without the Z texture where the draw is the one it expects; else the
    // quad draws as written. With its colour (clearQuadColour: the TEV's only stage outputs C0 and A0, `rgba`) the
    // renderer clears the colour too.
    enum { kMarkerClearQuad = 16 };

    inline void clearQuad(uint32_t z) {
        marker(kMarkerClearQuad, 0, z, 0);
    }

    inline void clearQuadColour(uint32_t z, uint32_t rgba) {
        marker(kMarkerClearQuad, 1, z, rgba);
    }

    // A scene bound (NEBULA_SCENE_*); the 3D scene's begin carries the camera (world to eye, 3x4) and the player's up.
    void sceneMarker(uint32_t marker, const float (*view)[4]);

    // A JPA stripe particle whose two strip vertices are drawn next: age << 16 | lifetime, strip 0 JPADrawStripe,
    // 1-2 the passes of JPADrawStripeX.
    void stripeParticle(uint64_t particle, uint32_t ageLifetime, uint8_t strip);
}  // namespace NebulaRender
