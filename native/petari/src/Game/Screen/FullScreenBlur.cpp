#include "Game/Screen/FullScreenBlur.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/System/NebulaFrameClock.hpp"

namespace MR {
    void drawFullScreenBlur(f32 param1) {
        drawFullScreenBlur(param1, param1, (s32)(param1 / 30.0f), (s32)((param1 / 30.0f) / 2.0f));
    }

    void drawFullScreenBlur(f32 param1, f32 param2, u8 param3, u8 param4) {
        f32 screenWidth = MR::getScreenWidth();

        GameTexture texture = GameTexture(MR::getScreenResTIMG());

        // The two layers (runtime/src/render/materials/tex_const_alpha.hlsl): the screen and last frame's blur, each at
        // its constant alpha, blended by it over the 2D view (TDDraw's draw mode 1, blend 1, view 2).
        SiteMaterial layer;
        TDDraw::setupNative(&layer, 1, 1, 2);
        layer.setShader(NEBULA_SHADER_TEX_CONST_ALPHA);
        layer.setColor(0, 0, 0, 0, param3);
        layer.setTexture(0, &texture);
        TypedDraw site = TDDraw::site(layer, 2);
        TDDraw::drawTexture(site, Vec2f(-param1, -param1), &texture, Vec2f(screenWidth + param1 * 2.0f, NebulaFrameClock::cScreenHeight + param1 * 2.0f));

        layer.setColor(0, 0, 0, 0, param4);
        layer.setTexture(0, MR::getFullScreenBlurTexture());
        TDDraw::drawTexture(site, Vec2f(-param2, -param2), MR::getFullScreenBlurTexture(), Vec2f(screenWidth + param2 * 2.0f, NebulaFrameClock::cScreenHeight + param2 * 2.0f));

        // The next frame's blur: the screen shrunk into the blur texture, a pass into its own target.
        GameTexture* pBlur = MR::getFullScreenBlurTexture();
        Frame::targetBegin(pBlur->image(), pBlur->shareAcross(), pBlur->shareDown(), 0);
        SiteMaterial shrink;
        TDDraw::setupNative(&shrink, 1, 1, 2);
        shrink.setTexture(0, &texture);
        TypedDraw site2 = TDDraw::site(shrink, 2);
        TDDraw::drawTexture(site2, Vec2f(0.0f, 0.0f), &texture, Vec2f(screenWidth, static_cast< f32 >(MR::getScreenHeight())));
        Frame::targetEnd(pBlur->image(), NEBULA_TARGET_RGB);
        MR::loadProjectionMtx();
        MR::loadViewMtx();
    }
};  // namespace MR
