#include "Game/Screen/ScreenBlurEffect.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Screen/CopyFilterNegater.hpp"
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/MathUtil.hpp"
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
    J3DNativeView::setProjection(projectionMtx, GX_PERSPECTIVE);

    TMtx34f posMtx;
    posMtx.identity();
    J3DNativeShape::sendPosition((MtxPtr)&posMtx, GX_PNMTX0);
    J3DNativeState::setCurrentMtx(GX_PNMTX0);

    J3DNativeState::setCullMode(GX_CULL_NONE);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    J3DNativeState::setNumChans(1);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setNumTexGens(2);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    J3DNativeState::setNumTevStages(2);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE1, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE1, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE1, GX_CA_TEXA, GX_CA_KONST, GX_CA_APREV, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE1, GX_TEV_COMP_R8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K0);
    J3DNativeState::setNumIndStages(0);
    J3DNativeState::setTevDirect(GX_TEVSTAGE0);
    J3DNativeState::setFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, Color8(0, 0, 0, 0));
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeState::setZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    J3DNativeState::setZCompLoc(GX_TRUE);
    J3DNativeState::setDither(GX_FALSE);
    J3DNativeState::setChanMatColor(GX_COLOR0A0, Color8(255, 255, 255, static_cast< s32 >(get_10() * 220.0f * mIntensity)));
    J3DNativeState::setChanAmbColor(GX_COLOR0A0, Color8(255, 255, 255, 255));
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    J3DNativeState::setTevKColor(GX_KCOLOR0, Color8(240, 240, 240, 240));

    _14->load(GX_TEXMAP0);
    _18->load(GX_TEXMAP1);

    f32 y = MR::tanDegree(MR::getRenderFovy() * 0.5f) * mScale;
    f32 x = MR::getAspect() * y;

    J3DNativeDraw::begin(GX_TRIANGLESTRIP, 4, NEBULA_SHAPE_TEXCOORD0);
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
    CopyFilterNegater copyFilterNegater = CopyFilterNegater();

    _14->capture(0, 0, GX_TF_RGBA8, false, 0);
    _18->capture(0, 0, GX_TF_Z8, true, 0);
}
