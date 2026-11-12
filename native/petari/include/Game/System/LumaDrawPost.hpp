#pragma once

// LumaCore's `draw.post` service (plan 2.4 layer 8; contract draw 15; native/include/luma_draw_post.h): the mods'
// full-screen pixel programs over the frame, as NEBULA_PASS_POST passes the host draws. The harness fact
// draw.post.<stage>.

#include <luma_service_provider.h>

namespace luma {
    namespace draw {
        namespace post {
            // A stage's passes (LumaDrawPostStage): WORLD from GameScene::draw3D after Frame::sceneEnd, FRAME from
            // GameScene::draw2D after the hud.after point. One load while no slot is set there.
            void stage(u32 stage);
            // The service's provider, registered by luma::services::provideOwn.
            const LumaServiceProvider* provider();
        }  // namespace post
    }  // namespace draw
}  // namespace luma
