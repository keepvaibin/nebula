#pragma once

#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>

// The renderer's signals from the game (galaxy/motion_signals.h): host markers (the game object whose drawing
// follows, scene signals), bloom phases, scene target clear quads, scene bounds and JPA stripe particles. Each reaches the
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
        host->memory_rewritten(host->user, p, size);
    }

    // A host marker, whatever the draw identities.
    inline void marker(uint8_t kind, uint16_t aux, uint64_t a, uint64_t b) {
        J3DNativeDraw::flush();
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
        kBloomHold = 1,
        kBloomNormal = 2,
        kBloomSimple = 3,
    };

    inline void bloomMarker(uint16_t phase, uint32_t a, uint32_t b) {
        marker(kMarkerBloom, phase, a, b);
    }

    // A scene bound (NEBULA_SCENE_*); the 3D scene's begin carries the camera (world to eye, 3x4) and the player's up.
    void sceneMarker(uint32_t marker, const float (*view)[4]);

}  // namespace NebulaRender
