#include "Game/MapObj/BigBubbleDrawer.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/Render/GameTexture.hpp"
#include <nebula_game_api.h>


namespace {
    static Color8 sStageColor0(0xE0E0E0FF);
    Color8 sStageColor1(0xFF000000);
    SiteMaterial sMaterial;
    Vec3f sEnvMatTrans(1.0f, 1.0f, 1.5f);
};  // namespace

BigBubbleDrawer::BigBubbleDrawer() {
    mBubbleTexture = nullptr;
    mRainbowTexture = nullptr;
    mBubbleTexture = new GameTexture(MR::loadTexFromArc("BigBubble", "Bubble.bti"));
    mRainbowTexture = new GameTexture(MR::loadTexFromArc("BigBubble", "Rainbow.bti"));
}

void BigBubbleDrawer::setUpDraw() const {
    setUpMaterial();
}

void BigBubbleDrawer::setWarningColor(const Color8& rColor) {
    sMaterial.setColor(4, rColor.r, rColor.g, rColor.b, rColor.a);
}

const SiteMaterial& BigBubbleDrawer::material() {
    return sMaterial;
}

void BigBubbleDrawer::setUpVertexFormat() const {
}

void BigBubbleDrawer::setUpTexture() const {
}

void BigBubbleDrawer::setUpStage() const {
    // runtime/src/render/materials/big_bubble.hlsl: the rainbow (texture 0) with sStageColor0 mixed in by the bubble
    // environment map's alpha (texture 1, by the eye-space normal offset by
    // sEnvMatTrans), sStageColor1 over it by its alpha; blended by alpha, depth tested and not written, back faces culled.
    SiteMaterial& material = sMaterial;
    material.init(NEBULA_SHADER_BIG_BUBBLE, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, false,
                       NEBULA_CULL_BACK);
    material.setColor(0, ::sStageColor0.r, ::sStageColor0.g, ::sStageColor0.b, ::sStageColor0.a);
    material.setColor(4, ::sStageColor1.r, ::sStageColor1.g, ::sStageColor1.b, ::sStageColor1.a);
    Pos3f env;
    env.identity();
    env.setTrans(::sEnvMatTrans);
    for (u32 row = 0; row < 3; row++) {
        for (u32 column = 0; column < 4; column++) {
            material.constants[8 + row * 4 + column] = env.mMtx[row][column];
        }
    }
    material.setTexture(0, mRainbowTexture);
    material.setTexture(1, mBubbleTexture);
}

void BigBubbleDrawer::setUpPixelFormat() const {
}

void BigBubbleDrawer::setUpMaterial() const {
    setUpVertexFormat();
    setUpTexture();
    setUpStage();
    setUpPixelFormat();
    MR::loadProjectionMtx();
}
