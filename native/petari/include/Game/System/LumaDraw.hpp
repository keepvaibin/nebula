#pragma once

// LumaCore's `draw` service (plan 2.4; contract draw): a mod draws at the frame's named points: in 2-D over the game
// (rectangles, lines, triangles, images, text in the game's fonts and the mod's own), clipped and transformed, and in
// 3-D in the world (FB's layers 3 to 8). Textures by content name or from the mod's pixels. Nothing a mod draws stays in
// the renderer: a mod that is off draws nothing from that frame.

#include <luma_draw.h>
#include <luma_service_provider.h>
#include <nebula_types.h>

class GameTexture;

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
        // NebulaArena::destroy: draw.mesh's models made in [pStart, pEnd) (the scene's) are forgotten.
        void released(const void* pStart, const void* pEnd);
        // For the 3-D calls (FB): the mod drawing now at a world point; null outside a world point's call.
        const LumaMod* inWorld();
        // The primitives `count` more for the mod drawing now: false past LUMA_DRAW_PRIMITIVES_MOST this frame (said
        // once a frame, Service.Limit), nothing to draw.
        bool count(const LumaMod* pMod, const char* pWhere, u32 count);
        // A movie's subtitle (luma::movie::drawSubtitles, GameScene::draw2D over the movie): `pText` (UTF-16) centred
        // low on the screen in the game's cinema font, white over a dark shadow, wrapped inside the 4:3 frame.
        void subtitle(const char16_t* pText);
        // draw.view (FB, layer 7): `pTexture` (GameTexture::createPixelTarget, w x h) becomes a texture of the mod's,
        // owned by its slot from now on (counted as w x h x 4 bytes; texture_free refuses it). LUMA_OK and *pOut, or
        // LUMA_E_LIMIT, reported, and pTexture deleted.
        s32 adoptTarget(const LumaMod* pMod, const char* pWhere, GameTexture* pTexture, u32 w, u32 h, LumaHandle* pOut);
        // The mod's target texture of that handle, or null (freed: the mod's end, dropTarget).
        GameTexture* targetOf(const LumaMod* pMod, LumaHandle handle);
        // The target freed (after this frame, as a texture from pixels); nothing for a handle that is not one.
        void dropTarget(const LumaMod* pMod, LumaHandle handle);
        // The service's provider, registered by luma::services::provideOwn.
        const LumaServiceProvider* provider();
    }  // namespace draw
}  // namespace luma
