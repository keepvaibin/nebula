#pragma once

// LumaCore's `draw.view` service (docs/modding/services/draw.md; native/include/luma_draw_view.h): a mod's views of
// the scene from its own cameras into textures of its draw service. The harness facts draw.view.<h> and
// draw.view.end.<h>.

#include <luma_service_provider.h>

namespace luma {
    namespace draw {
        namespace view {
            // GameScene::draw3D, before the scene begins (the frame's poses, entry lists and lights are this frame's):
            // each live view drawn into its texture. One load while no view lives.
            void render();
            // GameScene::calcAnim, before the models take their poses (SceneFunction::executeCalcViewAndEntryList): the
            // main camera a mod holds (main_eye) into the scene's camera context. One load while none is held.
            void mainCamera();
            // NebulaArena::destroy: views following an actor in [pStart, pEnd) stop following (the camera stays).
            void released(const void* pStart, const void* pEnd);
            // The service's provider, registered by luma::services::provideOwn.
            const LumaServiceProvider* provider();
        }  // namespace view
    }  // namespace draw
}  // namespace luma
