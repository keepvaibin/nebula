#pragma once

// LumaCore's `draw.view` service (docs/modding/reference/services/draw.md; native/include/luma_draw_view.h): a mod's views of
// the scene from its own cameras into textures of its draw service. The harness facts draw.view.<h> and
// draw.view.end.<h>.

#include <luma_service_provider.h>
#include <nebula_geometry.hpp>

namespace luma {
    namespace draw {
        namespace view {
            // GameScene::draw3D, before the scene begins (the frame's poses, entry lists and lights are this frame's):
            // each live view drawn into its texture. One load while no view lives.
            void render();
            // GameScene::calcAnim, before the models take their poses (SceneFunction::executeCalcViewAndEntryList): the
            // main camera a mod holds (main_eye, main_rig on this frame's joints) into the scene's camera context. One
            // load while none is held.
            void mainCamera();
            // SceneFunction::executeMovementList, before the scene's cameras move (MovementType_Camera): the game's own
            // view, field of view and near plane back in the context, so its camera works from its own pose. One load
            // while none is held.
            void mainCameraBeforeGame();
            // SceneFunction::executeMovementList, right after the scene's cameras move, before anything reads them (the
            // pointer's camera, the view load, clipping, the player's stick directions): the held main camera, on the
            // poses the frame starts with. One load while none is held.
            void mainCameraAfterGame();
            // MR::getCameraWatchPos: the point the held main camera looks at while it is in the context (null: the
            // game's camera's own), so everything placed by the camera's watch point (sound emitters, the ocean ring,
            // a mod's read of the camera) follows the main camera as the view does.
            const Vec3f* mainWatchPos();
            // NebulaArena::destroy: views following an actor in [pStart, pEnd) stop following (the camera stays); a
            // main rig on one gives the game its camera back at the next apply.
            void released(const void* pStart, const void* pEnd);
            // The service's provider, registered by luma::services::provideOwn.
            const LumaServiceProvider* provider();
        }  // namespace view
    }  // namespace draw
}  // namespace luma
