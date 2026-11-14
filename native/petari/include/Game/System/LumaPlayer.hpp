#pragma once

// LumaCore's `player` service: extra players, each a MarioActor of the game's own beside the
// scene's player, driven by the second controller or by nothing.

#include <luma_service_provider.h>

namespace luma {
    namespace player {
        // NebulaArena::destroy, before its memory goes: extra players in [pStart, pEnd) are forgotten (their scene
        // went), and the second controller is the co-star's again. One load while no extra player stands.
        void released(const void* pStart, const void* pEnd);
        // The service's provider, registered by luma::services::provideOwn.
        const LumaServiceProvider* provider();
    }
}
