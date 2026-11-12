#pragma once

// LumaCore's `draw.inst` service (plan 2.4 layer 5; contract draw; native/include/luma_draw_inst.h): a mod's overrides
// of one actor's drawn model: colours, a material's colour by name, parts hidden, a drawn scale. The harness facts
// draw.inst.of, draw.inst and draw.inst.end.

#include <luma_service_provider.h>

namespace luma {
    namespace draw {
        namespace inst {
            // GameScene::calcAnim, after the game's animation, view and entry lists: the overrides onto this frame's
            // materials and draw poses. One load while no override lives.
            void frame();
            // NebulaArena::destroy: overrides of actors in [pStart, pEnd) end, never touched again.
            void released(const void* pStart, const void* pEnd);
            // The service's provider, registered by luma::services::provideOwn.
            const LumaServiceProvider* provider();
        }
    }
}
