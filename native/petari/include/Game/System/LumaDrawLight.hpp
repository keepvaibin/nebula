#pragma once

// LumaCore's `draw.light` service (docs/modding/services/draw.md; native/include/luma_draw_light.h): a mod's own
// lights in the world. Each frame drawn, each is a lamp of the scene (a world light, NEBULA_WORLD_LIGHT_REQUEST: the
// renderer's lights) and asks for the game's one point light as the game's lamps do (LightPointCtrl: the nearest to
// the player is lit). The harness facts draw.light.add, draw.light, draw.light.end.

#include <luma_service_provider.h>

namespace luma {
    namespace draw {
        namespace light {
            // GameLightSignal::frame (GameScene::draw3D's begin): the mods' lights into this frame. One load while no
            // mod has a light.
            void frame();
            // The service's provider, registered by luma::services::provideOwn.
            const LumaServiceProvider* provider();
        }
    }
}
