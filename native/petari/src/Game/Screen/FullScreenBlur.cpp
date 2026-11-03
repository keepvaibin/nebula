#include "Game/Screen/FullScreenBlur.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include "Game/System/NebulaFrameClock.hpp"

namespace MR {
    void drawFullScreenBlur(f32 param1) {
        drawFullScreenBlur(param1, param1, (s32)(param1 / 30.0f), (s32)((param1 / 30.0f) / 2.0f));
    }

    void drawFullScreenBlur(f32 param1, f32 param2, u8 param3, u8 param4) {
        f32 screenWidth = MR::getScreenWidth();

        JUTTexture texture = JUTTexture(MR::getScreenResTIMG());

        // The two layers (runtime/src/render/materials/tex_const_alpha.hlsl): the screen and last frame's blur, each at
        // its constant alpha, blended by it over the 2D view (TDDraw's draw mode 1, blend 1, view 2).
        NebulaNativeMaterial layer;
        TDDraw::setupNative(&layer, 1, 1, 2);
        layer.shader = NEBULA_SHADER_TEX_CONST_ALPHA;
        NativeShader::setColor(layer, 0, 0, 0, 0, param3);
        NativeShader::setTexture(layer, 0, &texture);
        NativeShader::send(layer);
        TDDraw::drawTexture(TVec2f(-param1, -param1), &texture, TVec2f(screenWidth + param1 * 2.0f, NebulaFrameClock::cScreenHeight + param1 * 2.0f));

        NativeShader::setColor(layer, 0, 0, 0, 0, param4);
        NativeShader::setTexture(layer, 0, MR::getFullScreenBlurTexture());
        NativeShader::send(layer);
        TDDraw::drawTexture(TVec2f(-param2, -param2), MR::getFullScreenBlurTexture(), TVec2f(screenWidth + param2 * 2.0f, NebulaFrameClock::cScreenHeight + param2 * 2.0f));

        // The next frame's blur: the screen shrunk into the blur texture (128x64), a pass into its own target.
        JUTTexture* pBlur = MR::getFullScreenBlurTexture();
        J3DNativeTarget::begin(pBlur->mImage, pBlur->getWidth(), pBlur->getHeight(), 0);
        NebulaNativeMaterial shrink;
        TDDraw::setupNative(&shrink, 1, 1, 2);
        NativeShader::setTexture(shrink, 0, &texture);
        NativeShader::send(shrink);
        TDDraw::drawTexture(TVec2f(0.0f, 0.0f), &texture, TVec2f((screenWidth / static_cast< f32 >(NebulaFrameClock::cScreenUnitsAcross)) * 128.0f, 64.0f));
        J3DNativeTarget::end(pBlur->mImage, NEBULA_TARGET_RGB);
        MR::loadProjectionMtx();
        MR::loadViewMtx();
    }
};  // namespace MR
