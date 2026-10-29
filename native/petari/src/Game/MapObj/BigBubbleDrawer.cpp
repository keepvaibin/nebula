#include "Game/MapObj/BigBubbleDrawer.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>

namespace {
    static Color8 sTevColor0(0xE0E0E0FF);
    Color8 sTevColor1(0xFF000000);
    TVec3f sEnvMatTrans(1.0f, 1.0f, 1.5f);
};  // namespace

BigBubbleDrawer::BigBubbleDrawer() {
    mBubbleTexture = nullptr;
    mRainbowTexture = nullptr;
    _8 = 0.0f;
    mBubbleTexture = new JUTTexture(MR::loadTexFromArc("BigBubble.arc", "Bubble.bti"), 0);
    mRainbowTexture = new JUTTexture(MR::loadTexFromArc("BigBubble.arc", "Rainbow.bti"), 0);
}

void BigBubbleDrawer::setUpDraw() const {
    setUpMaterial();
}

void BigBubbleDrawer::setUpVertexFormat() const {
}

void BigBubbleDrawer::setUpTexture() const {
    J3DNativeState::setNumTexGens(2);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x1E, 0, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX3x4, GX_TG_NRM, 0x21, 0, 0x7D);
    TPos3f mtx;
    mtx.identity();
    J3DNativeShape::sendTexture(mtx, 0x1E, GX_MTX2x4);

    mtx.set(MR::getCameraViewMtx());
    mtx.setTrans(::sEnvMatTrans);

    J3DNativeShape::sendTexture(mtx, 0x21, GX_MTX3x4);
    mRainbowTexture->load(GX_TEXMAP0);
    mBubbleTexture->load(GX_TEXMAP1);
}

void BigBubbleDrawer::setUpTevStage() const {
    J3DNativeState::setNumTevStages(3);
    J3DNativeState::setTevColor(GX_TEVREG0, (GXColor&)::sTevColor0);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 0, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 0, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_C0, GX_CC_TEXA, GX_CC_CPREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE1, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 0, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE2, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE2, GX_CC_CPREV, GX_CC_C1, GX_CC_A1, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 1, GX_TEVPREV);
    J3DNativeState::setTevKAlphaSel(GX_TEVSTAGE2, GX_TEV_KASEL_7_8);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE2, GX_CA_APREV, GX_CA_KONST, GX_CA_A1, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, 0, GX_TEVPREV);
}

void BigBubbleDrawer::setUpPixelFormat() const {
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    J3DNativeState::setColorUpdate(1);
    J3DNativeState::setAlphaUpdate(0);
    J3DNativeState::setCoPlanar(0);
    J3DNativeState::setZMode(1, GX_LEQUAL, 0);
    J3DNativeState::setZCompLoc(0);
    J3DNativeState::setCullMode(GX_CULL_BACK);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
}

void BigBubbleDrawer::setUpMaterial() const {
    setUpVertexFormat();
    J3DNativeState::setNumChans(1);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, 0, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanCtrl(GX_COLOR1A1, 0, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    setUpTexture();
    setUpTevStage();
    setUpPixelFormat();
    J3DNativeShape::sendPosition(MR::getCameraViewMtx(), 0);

    Mtx normalMtx;
    PSMTXIdentity(normalMtx);
    J3DNativeShape::sendNormal(normalMtx, 0);
    J3DNativeState::setCurrentMtx(0);
    MR::loadProjectionMtx();
}
