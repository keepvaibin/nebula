#pragma once

// LumaCore's `draw` service (plan 2.4; contract draw): a mod draws at the frame's named points: in 2-D over the game
// (rectangles, lines, triangles, images, text in the game's fonts and the mod's own), clipped and transformed, and in
// 3-D in the world (FB's layers 3 to 8). Textures by content name or from the mod's pixels. Nothing a mod draws stays in
// the renderer: a mod that is off draws nothing from that frame.

#include <luma_draw.h>
#include <luma_service_provider.h>
#include <nebula_types.h>

namespace luma {
    namespace draw {
        // The frame's points (LumaDrawPoint): GameScene::draw3D (the world points), GameScene::draw2D (hud.before,
        // hud.after), GameSystem::draw (loading, present). Calls the mods' functions set for it, bottom of the Mods
        // list first. One load while no mod draws at it.
        void point(u32 point);
        // GameSystem::draw: true while the game loads a scene (the `loading` point's moment).
        bool isLoading();
        // GameSystem::update, after the services' frame start: textures freed last frame leave the renderer. One load
        // while none was freed.
        void frame();
        // For the 3-D calls (FB): the mod drawing now at a world point; null outside a world point's call.
        const LumaMod* inWorld();
        // The primitives `count` more for the mod drawing now: false past LUMA_DRAW_PRIMITIVES_MOST this frame (said
        // once a frame, Service.Limit), nothing to draw.
        bool count(const LumaMod* pMod, const char* pWhere, u32 count);
        // The service's provider, registered by luma::services::provideOwn.
        const LumaServiceProvider* provider();
    }  // namespace draw
}  // namespace luma
