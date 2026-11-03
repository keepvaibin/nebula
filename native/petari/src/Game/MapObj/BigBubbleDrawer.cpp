#include "Game/MapObj/BigBubbleDrawer.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <cstring>


namespace {
    static Color8 sTevColor0(0xE0E0E0FF);
    Color8 sTevColor1(0xFF000000);
    TVec3f sEnvMatTrans(1.0f, 1.0f, 1.5f);
};  // namespace

BigBubbleDrawer::BigBubbleDrawer() {
    mBubbleTexture = nullptr;
    mRainbowTexture = nullptr;
    _8 = 0.0f;
    mBubbleTexture = new JUTTexture(MR::loadTexFromArc("BigBubble.arc", "Bubble.bti"));
    mRainbowTexture = new JUTTexture(MR::loadTexFromArc("BigBubble.arc", "Rainbow.bti"));
}

void BigBubbleDrawer::setUpDraw() const {
    setUpMaterial();
}

void BigBubbleDrawer::setUpVertexFormat() const {
}

void BigBubbleDrawer::setUpTexture() const {
}

void BigBubbleDrawer::setUpStage() const {
    // runtime/src/render/materials/big_bubble.hlsl: the rainbow (texture 0) with sTevColor0 mixed in by the bubble
    // environment map's alpha (texture 1, by the eye-space normal through the camera's rotation offset by
    // sEnvMatTrans), sTevColor1 over it by its alpha; blended by alpha, depth tested and not written, back faces culled.
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_BIG_BUBBLE, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, false,
                       NEBULA_CULL_BACK);
    NativeShader::setColor(material, 0, ::sTevColor0.r, ::sTevColor0.g, ::sTevColor0.b, ::sTevColor0.a);
    NativeShader::setColor(material, 4, ::sTevColor1.r, ::sTevColor1.g, ::sTevColor1.b, ::sTevColor1.a);
    TPos3f env;
    env.set(MR::getCameraViewMtx());
    env.setTrans(::sEnvMatTrans);
    for (u32 row = 0; row < 3; row++) {
        for (u32 column = 0; column < 4; column++) {
            material.constants[8 + row * 4 + column] = env.mMtx[row][column];
        }
    }
    NativeShader::setTexture(material, 0, mRainbowTexture);
    NativeShader::setTexture(material, 1, mBubbleTexture);
    NativeShader::send(material);
}

void BigBubbleDrawer::setUpPixelFormat() const {
    J3DPass::writeColor(true);
    J3DPass::writeAlpha(false);
}

void BigBubbleDrawer::setUpMaterial() const {
    setUpVertexFormat();
    setUpTexture();
    setUpStage();
    setUpPixelFormat();
    J3DNativeShape::setPosition(0, MR::getCameraViewMtx());

    Mtx normalMtx;
    NebulaMtxIdentity(normalMtx);
    J3DNativeShape::setNormal(0, normalMtx);
    J3DPass::positionMatrix(0);
    MR::loadProjectionMtx();
}
