#include "Game/Screen/ScreenBlurEffect.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include "Game/Util/NativeShader.hpp"
#include <nebula_game_api.h>

namespace {
    // runtime/src/render/materials/screen_blur.hlsl: the blurred picture (texture 0) at the effect's alpha where the
    // depth capture (texture 1) lies beyond 240 (of 255); depth tested, not written; blended by its alpha.
    void sendBlurMaterial(JUTTexture* pPicture, JUTTexture* pDepth, u8 alpha) {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_SCREEN_BLUR, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, false,
                           NEBULA_CULL_NONE);
        material.constants[0] = alpha / 255.0f;
        material.constants[1] = 240 / 255.0f;
        NativeShader::setTexture(material, 0, pPicture);
        NativeShader::setTexture(material, 1, pDepth);
        NativeShader::send(material);
        J3DNativeShape::setCurrentIdentity();
    }
}  // namespace
#include <JSystem/JGeometry/TMatrix.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>

namespace {
    static const f32 sScale = 10000.0f;
};  // namespace

ScreenBlurEffect::ScreenBlurEffect(const char* pName) : ImageEffectBase(pName), _14(), _18(), mScale(::sScale), _20(true), mIntensity(1.0f) {
    MR::connectToSceneImageEffect(this);
    MR::createImageEffectSystemHolder();
    MR::getImageEffectResource()->createBlurTexture();

    _14 = MR::getImageEffectResource()->_0;
    _18 = MR::getImageEffectResource()->_8;
    mFrameDepthKey = ImageEffectLocalUtil::createFrameKey();
}

void ScreenBlurEffect::calcAnimSub() {
    _20 = false;
}

void ScreenBlurEffect::notifyTurnOn() {
    _20 = true;
}

void ScreenBlurEffect::draw() const {
    if (isSomething()) {
        drawBlur();
        captureBlur();
    }
}

void ScreenBlurEffect::drawBlur() const {
    if (_20) {
        return;
    }

    TProj3f projectionMtx;
    projectionMtx.makePerspective(MR::getRenderFovy(), MR::getAspect(), MR::getNearZ(), MR::getFarZ());
    J3DNativeView::setPerspective(projectionMtx);

    TMtx34f posMtx;
    posMtx.identity();
    J3DNativeShape::setPosition(0, (MtxPtr)&posMtx);
    J3DPass::positionMatrix(0);

    J3DPass::writeColor(true);
    J3DPass::writeAlpha(false);
    sendBlurMaterial(_14, _18, static_cast< u8 >(static_cast< s32 >(get_10() * 220.0f * mIntensity)));

    f32 y = MR::tanDegree(MR::getRenderFovy() * 0.5f) * mScale;
    f32 x = MR::getAspect() * y;

    J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4, NEBULA_SHAPE_TEXCOORD0);
    {
        f32 z = -mScale;

        J3DNativeDraw::position(-x, -y, z);
        J3DNativeDraw::texCoord(0.0f, 1.0f);

        J3DNativeDraw::position(-x, y, z);
        J3DNativeDraw::texCoord(0.0f, 0.0f);

        J3DNativeDraw::position(x, -y, z);
        J3DNativeDraw::texCoord(1.0f, 1.0f);

        J3DNativeDraw::position(x, y, z);
        J3DNativeDraw::texCoord(1.0f, 0.0f);
    }
}

void ScreenBlurEffect::captureBlur() const {
    _14->frameColor();
    ImageEffectLocalUtil::drawFrameDepth(_18, mFrameDepthKey);
    MR::loadProjectionMtx();
    MR::loadViewMtx();
}
