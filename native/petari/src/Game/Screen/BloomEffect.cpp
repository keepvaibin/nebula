#include "Game/Screen/BloomEffect.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <NebulaGpuTypes.h>
#include <nebula_game_api.h>
#include <cstring>
#include "Game/Util/RenderSignals.hpp"
#include "Game/System/NebulaHost.hpp"

namespace {
    const u32 sL1RoundPoints = 6;
    const u32 sL2RoundPoints = 12;

    // Whether the renderer's bloom is there (NEBULA_OPTION_BLOOM): decided at the frame's first pass, so a change in
    // between never draws a bloom over a scene that was not kept.
    u32 sBloomRoute;

    u32 floatBits(f32 value) {
        u32 bits;
        __builtin_memcpy(&bits, &value, sizeof(bits));
        return bits;
    }
};  // namespace

BloomEffect::BloomEffect(const char* pName) : ImageEffectBase(pName), _14(255), _18(1.0f / ::sL1RoundPoints), _1C(1.0f / ::sL2RoundPoints), _20(128) {
    MR::connectToSceneNormalBloom(this);
    MR::createImageEffectSystemHolder();
}

void BloomEffect::preDraw() const {
    if (!isSomething()) {
        return;
    }

    sBloomRoute = NebulaHost::option(NEBULA_OPTION_BLOOM);
    if (sBloomRoute == 0) {
        return;
    }
    NebulaRender::bloomMarker(NebulaRender::kBloomHold, 0, 0);

    MR::fillScreen(Color8(0, 0, 0, 0xFF).mRGBA);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
}

void BloomEffect::postDraw() const {
    if (!isSomething() || sBloomRoute == 0) {
        return;
    }

    // Drawn by the renderer at the frame's own size: the threshold, both blurs' tap weights, the intensity and the
    // rings' horizontal radius per unit.
    const u8 intensity = _14 * get_10();
    const u32 tap1 = static_cast< u8 >(static_cast< s32 >(_18 * 255.0f));
    const u32 tap2 = static_cast< u8 >(static_cast< s32 >(_1C * 255.0f));
    NebulaRender::bloomMarker(NebulaRender::kBloomNormal, _20 | tap1 << 8 | tap2 << 16 | static_cast< u32 >(intensity) << 24,
                              floatBits(1.0f / MR::getAspect()));
    initDraw();
    J3DPass::blend(NEBULA_FACTOR_ONE, NEBULA_FACTOR_ONE);
}

u8 BloomEffect::getIntensity1Default() const {
    return 42;
}

u8 BloomEffect::getIntensity2Default() const {
    return 21;
}

bool BloomEffect::isForEachModel() const {
    return true;
}

void BloomEffect::initDraw() const {
    MR::resetDrawState();
    J3DPass::depth(false, NEBULA_COMPARE_ALWAYS, false);

    Mtx44 projectionMtx;
    NebulaMtxOrtho(projectionMtx, 0.0f, MR::getScreenHeight(), 0.0f, MR::getScreenWidth(), -10000.0f, 10000.0f);
    J3DNativeView::setOrthographic(projectionMtx);

    MR::loadViewMtxFor2DModel();

    TMtx34f posMtx;
    posMtx.identity();
    J3DNativeShape::setPosition(0, posMtx.toMtxPtr());
    J3DPass::positionMatrix(0);

    J3DPass::cull(NEBULA_CULL_NONE);
    J3DPass::writeColor(true);
    J3DPass::writeAlpha(false);
    J3DPass::depth(false, NEBULA_COMPARE_ALWAYS, false);
}
