#include "Game/MapObj/ClipFieldFillDraw.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/ScreenAlphaCapture.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"

ClipFieldFillDraw::ClipFieldFillDraw(const char* pName)
    : NameObj(pName), mFillBlendMode(GX_BM_BLEND), mFillSrcFactor(GX_BL_SRCALPHA), mFillDstFactor(GX_BL_ONE), mFillLogicOp(GX_LO_NOOP),
      _1C(255, 230, 80, 200), _20(255, 255, 255, 255), _24(), _25() {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_ClipFieldFillDraw);
    MR::createScreenAlphaSceneObj(1, 1.0f);
}

void ClipFieldFillDraw::init(const JMapInfoIter& rIter) {
    if (MR::isValidInfo(rIter)) {
        return;
    }
}

void ClipFieldFillDraw::setModeSubColor() {
    mFillBlendMode = GX_BM_SUBTRACT;
    mFillDstFactor = GX_BL_ONE;
    mFillSrcFactor = GX_BL_ONE;
    mFillLogicOp = GX_LO_NOOP;
}

void ClipFieldFillDraw::setUpFillScreen() const {
    Mtx44 projectionMtx;
    C_MTXOrtho(projectionMtx, 0.0f, MR::getScreenHeight(), 0.0f, MR::getScreenWidth(), -1.0f, 1.0f);
    J3DNativeView::setProjection(projectionMtx, GX_ORTHOGRAPHIC);

    TMtx34f posMtx;
    posMtx.identity();
    J3DNativeShape::sendPosition((MtxPtr)&posMtx, GX_PNMTX0);
    J3DNativeState::setCurrentMtx(GX_PNMTX0);

    J3DNativeState::setNumChans(1);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setNumTexGens(1);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    J3DNativeState::setNumTevStages(1);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

    GXTevAlphaArg alphaA;
    GXTevAlphaArg alphaB;

    if (_25) {
        alphaA = GX_CA_TEXA;
        alphaB = GX_CA_KONST;
    } else {
        alphaA = GX_CA_KONST;
        alphaB = GX_CA_TEXA;
    }

    J3DNativeState::setTevKColor(GX_KCOLOR0, Color8(0, 0, 0, 1));
    J3DNativeState::setTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, alphaA, alphaB, GX_CA_A0, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_COMP_RGB8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
    J3DNativeState::setZMode(GX_TRUE, GX_ALWAYS, GX_FALSE);
    J3DNativeState::setZCompLoc(GX_FALSE);
    J3DNativeState::setCullMode(GX_CULL_BACK);
    J3DNativeState::setDither(GX_FALSE);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeState::setBlendMode(mFillBlendMode, mFillSrcFactor, mFillDstFactor, mFillLogicOp);
}

void ClipFieldFillDraw::sendFillScreen(GXColor color) const {
    J3DNativeState::setTevColor(GX_TEVREG0, color);

    J3DNativeDraw::begin(GX_TRIANGLESTRIP, 4, NEBULA_SHAPE_TEXCOORD0);
    {
        u16 screenWidth = MR::getScreenWidth();
        u16 screenHeight = MR::getScreenHeight();

        J3DNativeDraw::position(0.0f, 0.0f, 0.0f);
        J3DNativeDraw::texCoord(0.0f, 0.0f);

        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(screenWidth)), 0.0f, 0.0f);
        J3DNativeDraw::texCoord(1.0f, 0.0f);

        J3DNativeDraw::position(0.0f, static_cast< f32 >(static_cast< u16 >(screenHeight)), 0.0f);
        J3DNativeDraw::texCoord(0.0f, 1.0f);

        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(screenWidth)), static_cast< f32 >(static_cast< u16 >(screenHeight)), 0.0f);
        J3DNativeDraw::texCoord(1.0f, 1.0f);
    }
}

void ClipFieldFillDraw::draw() const {
    MR::loadScreenAlphaTexture(1, GX_TEXMAP0);
    setUpFillScreen();
    sendFillScreen(_1C);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    J3DNativeView::setZScaleOffset(1.0f, 0.0f);
    MR::clearAlphaBuffer(0);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeState::setColorUpdate(GX_TRUE);

    if (_24) {
        TDDraw::setup(0, 1, 0);
        J3DNativeState::setCullMode(GX_CULL_FRONT);
        J3DNativeState::setZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    }
}

namespace MR {
    NameObj* createClipFieldFillDraw(const char* pParam1) {
        return static_cast< ClipFieldFillDraw* >(MR::createSceneObj(SceneObj_ClipFieldFillDraw));
    }

    NameObj* createVolumeDrawSpotLight(const char* pParam1) {
        ClipFieldFillDraw* clipFieldFillDraw;

        clipFieldFillDraw = static_cast< ClipFieldFillDraw* >(createClipFieldFillDraw(pParam1));
        clipFieldFillDraw->_25 = false;
        clipFieldFillDraw->_24 = false;
        clipFieldFillDraw->setModeSubColor();
        clipFieldFillDraw->_1C = static_cast< u32 >(Color8(70, 35, 11, 80));
        clipFieldFillDraw->_20 = static_cast< u32 >(Color8(255, 255, 128, 80));

        return clipFieldFillDraw;
    }
};  // namespace MR
