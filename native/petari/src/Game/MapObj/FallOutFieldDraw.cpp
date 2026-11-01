#include "Game/MapObj/FallOutFieldDraw.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);
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

namespace {
    // The fill (runtime/src/render/materials/image_effect.hlsl mode 2 or 3): the fill colour where the screen's alpha
    // mask (texture 0) is under or, by arg 0, over 1 (GX: konst 0's alpha compared), nothing elsewhere; blended by its
    // alpha, depth always passing and written, no culling.
    void sendFillMaterial(bool textureFirst) {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_IMAGE_EFFECT, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, true,
                           NEBULA_CULL_NONE);
        NativeShader::setColor(material, 4, 0, 0, 0, 1);
        NativeShader::setColor(material, 8, ::sFillColor.r, ::sFillColor.g, ::sFillColor.b, ::sFillColor.a);
        material.constants[12] = textureFirst ? 3.0f : 2.0f;
        material.constants[13] = 1.0f;
        NativeShader::setTexture(material, 0, MR::getScreenAlphaTexture(2));
        NativeShader::send(material);
    }
}  // namespace

void FallOutFieldDraw::setUpFillScreen() const {
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeView::setZScaleOffset(0.0f, 1.0f);
    sendFillMaterial(_C);
}


void FallOutFieldDraw::setUpEdgeAndClearAlpha() const {
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

    ImageEffectLocalUtil::capture(pScreenAlphaTexture2, 0, 1, GX_CTF_A8, true, 0, true);

    J3DNativeState::setColorUpdate(GX_FALSE);
    J3DNativeState::setAlphaUpdate(GX_TRUE);
    J3DNativeState::setDstAlpha(GX_FALSE, 0);
    J3DNativeView::setZScaleOffset(0.0f, 0.0f);
    // The mask: alpha 255 where the screen's alpha is under (or, by arg 0, over) 1 (GX: konst 0's alpha compared).
    const GXColor konst = {0, 0, 0, 1};
    const GXColor white = {255, 255, 255, 255};
    ImageEffectLocalUtil::setMode(_C ? ImageEffectLocalUtil::Mode_MaskAbove : ImageEffectLocalUtil::Mode_MaskBelow, konst,
                                  white);

    ImageEffectLocalUtil::drawTexture(pScreenAlphaTexture2, 2, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    ImageEffectLocalUtil::capture(pScreenAlphaTexture3, 2, 0, GX_CTF_A8, true, 0, true);

    ImageEffectLocalUtil::setMode(ImageEffectLocalUtil::Mode_Alpha, konst, ::sEdgeColor);

    ImageEffectLocalUtil::blurTexture(pScreenAlphaTexture3, 4, 4, 5, 0.004f, 1.0f);
    ImageEffectLocalUtil::capture(pScreenAlphaTexture4, 4, 4, GX_CTF_A8, false, 0, true);
    setUpEdgeAndClearAlpha();
    ImageEffectLocalUtil::setMode(ImageEffectLocalUtil::Mode_Edge, konst, ::sEdgeColor);
    ImageEffectLocalUtil::drawTexture(pScreenAlphaTexture4, 1, 0, 255, ImageEffectLocalUtil::TexDrawType_2);
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
