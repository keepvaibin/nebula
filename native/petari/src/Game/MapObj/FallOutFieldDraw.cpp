#include "Game/MapObj/FallOutFieldDraw.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/MapObj/ClipAreaHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ScreenAlphaCapture.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"

void FallOutFieldDraw_FORCE_MATCH_SDATA2() {
    (void)0.25f;
}

namespace {
    inline void drawFillScreen(GXColor color) {
        J3DNativeState::setTevColor(GX_TEVREG0, color);
        ImageEffectLocalUtil::sendTextureVertex(1, 0);
    }
};  // namespace

namespace {
    static GXTevOp sAlphaTevOperater = GX_TEV_COMP_A8_GT;
    static GXBlendMode sFillBlendMode = GX_BM_BLEND;
    static GXBlendFactor sFillSrcFactor = GX_BL_SRCALPHA;
    static GXBlendFactor sFillDstFactor = GX_BL_INVSRCALPHA;
    static GXLogicOp sFillLogicOp = GX_LO_NOOP;
    // static const s32 sBlurCount = _;
    // static const f32 sBlurRadius = _;
    // static const f32 sBlurIntensity = _;
    static Color8 sFillColor = Color8(0, 0, 0, 255);
    static Color8 sEdgeColor = Color8(0, 46, 200, 128);
};  // namespace

FallOutFieldDraw::FallOutFieldDraw(const char* pName) : NameObj(pName), _C() {
    MR::createClipAreaHolder();
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_FallOutFieldDraw);
    MR::createScreenAlphaSceneObj(2, 0.5f);
    MR::createScreenAlphaSceneObj(3, 0.25f);
    MR::createScreenAlphaSceneObj(4, 0.25f);
}

void FallOutFieldDraw::setUpFillScreen() const {
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevKColor(GX_KCOLOR0, Color8(0, 0, 0, 1));
    J3DNativeState::setTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);

    GXTevAlphaArg alphaA = _C ? GX_CA_TEXA : GX_CA_KONST;
    GXTevAlphaArg alphaB = _C ? GX_CA_KONST : GX_CA_TEXA;

    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, alphaA, alphaB, GX_CA_A0, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, ::sAlphaTevOperater, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
    J3DNativeState::setZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
    J3DNativeState::setZCompLoc(GX_FALSE);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeView::setZScaleOffset(0.0f, 1.0f);
    J3DNativeState::setBlendMode(::sFillBlendMode, ::sFillSrcFactor, ::sFillDstFactor, ::sFillLogicOp);
}

void FallOutFieldDraw::setUpEdgeAndClearAlpha() const {
    J3DNativeState::setTevColor(GX_TEVREG1, ::sEdgeColor);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_C1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_A1, GX_CA_TEXA, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_TRUE);
    J3DNativeState::setDstAlpha(GX_TRUE, 0);
}

void FallOutFieldDraw::init(const JMapInfoIter& rIter) {
    if (!MR::isValidInfo(rIter)) {
        return;
    }

    bool arg0;
    MR::getJMapInfoArg0WithInit(rIter, &arg0);
    _C = arg0;

    if (MR::isExistStageSwitchAppear(rIter)) {
        MR::listenNameObjStageSwitchOnOffAppear(this, MR::createStageSwitchCtrl(this, rIter), MR::Functor(this, &FallOutFieldDraw::activate),
                                                MR::Functor(this, &FallOutFieldDraw::deactivate));
        MR::deactivateClipArea();
    } else {
        MR::activateClipArea();
    }
}

void FallOutFieldDraw::activate() {
    MR::activateClipArea();
}

void FallOutFieldDraw::deactivate() {
    MR::deactivateClipArea();
}

void FallOutFieldDraw::draw() const {
    if (!MR::isActiveClipArea()) {
        return;
    }

    ImageEffectLocalUtil::setupDrawTexture();

    JUTTexture* pScreenAlphaTexture2 = MR::getScreenAlphaTexture(2);
    JUTTexture* pScreenAlphaTexture3 = MR::getScreenAlphaTexture(3);
    JUTTexture* pScreenAlphaTexture4 = MR::getScreenAlphaTexture(4);

    ImageEffectLocalUtil::capture(pScreenAlphaTexture2, 0, 1, GX_CTF_A8, true, 0);

    J3DNativeState::setTevKColor(GX_KCOLOR0, Color8(0, 0, 0, 1));
    J3DNativeState::setTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
    J3DNativeState::setTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0_A);
    J3DNativeState::setTevColor(GX_TEVREG0, Color8(255, 255, 255, 255));

    GXTevColorArg colorA = _C ? GX_CC_TEXA : GX_CC_KONST;
    GXTevColorArg colorB = _C ? GX_CC_KONST : GX_CC_TEXA;

    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, colorA, colorB, GX_CC_ZERO, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_COMP_RGB8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

    GXTevAlphaArg alphaA = _C ? GX_CA_TEXA : GX_CA_KONST;
    GXTevAlphaArg alphaB = _C ? GX_CA_KONST : GX_CA_TEXA;

    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, alphaA, alphaB, GX_CA_A0, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_COMP_A8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setColorUpdate(GX_FALSE);
    J3DNativeState::setAlphaUpdate(GX_TRUE);
    J3DNativeState::setDstAlpha(GX_FALSE, 0);
    J3DNativeView::setZScaleOffset(0.0f, 0.0f);

    ImageEffectLocalUtil::drawTexture(pScreenAlphaTexture2, 2, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    ImageEffectLocalUtil::capture(pScreenAlphaTexture3, 2, 0, GX_CTF_A8, true, 0);

    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_TEXA, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

    ImageEffectLocalUtil::blurTexture(pScreenAlphaTexture3, 4, 4, 5, 0.004f, 1.0f);
    ImageEffectLocalUtil::capture(pScreenAlphaTexture4, 4, 4, GX_CTF_A8, false, 0);
    setUpEdgeAndClearAlpha();
    ImageEffectLocalUtil::drawTexture(pScreenAlphaTexture4, 1, 0, 255, ImageEffectLocalUtil::TexDrawType_2);
    MR::loadScreenAlphaTexture(2, GX_TEXMAP0);
    setUpFillScreen();

    ::drawFillScreen(::sFillColor);

    MR::loadViewMtx();
    MR::loadProjectionMtx();

    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_TRUE);
    J3DNativeState::setDstAlpha(GX_TRUE, 0);
    J3DNativeView::setZScaleOffset(1.0f, 0.0f);
}

namespace MR {
    NameObj* createFallOutFieldDraw(const char*) {
        return MR::createSceneObj(SceneObj_FallOutFieldDraw);
    }
};  // namespace MR
