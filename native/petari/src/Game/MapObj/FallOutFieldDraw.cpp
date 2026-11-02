#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
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

namespace {
    inline void drawFillScreen(NebulaColor8 color) {
        ImageEffectLocalUtil::sendTextureVertex(1, 0);
    }
};  // namespace

namespace {
    static GpuTevOp sAlphaTevOperater = GPU_TEV_COMP_A8_GT;
    static GpuBlendMode sFillBlendMode = GPU_BM_BLEND;
    static GpuBlendFactor sFillSrcFactor = GPU_BL_SRCALPHA;
    static GpuBlendFactor sFillDstFactor = GPU_BL_INVSRCALPHA;
    static GpuLogicOp sFillLogicOp = GPU_LO_NOOP;
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
    mMask = new JUTTexture(MR::getCanvasWidth() / 2, MR::getCanvasHeight() / 2, GPU_TF_I8);
    mMask->mWrapS = GPU_CLAMP;
    mMask->mWrapT = GPU_CLAMP;
    mMask->mMinType = GPU_LINEAR;
    mMask->mMagType = GPU_LINEAR;
}

namespace {
    // The fill (runtime/src/render/materials/image_effect.hlsl mode 2 or 3): the fill colour where the screen's alpha
    // mask (texture 0) is under or, by arg 0, over 1 (console: konst 0's alpha compared), nothing elsewhere; blended by its
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
    J3DPass::writeColor(true);
    J3DPass::writeAlpha(false);
    J3DNativeView::setZScaleOffset(0.0f, 1.0f);
    sendFillMaterial(_C);
}


void FallOutFieldDraw::setUpEdgeAndClearAlpha() const {
    J3DPass::writeColor(true);
    J3DPass::writeAlpha(true);
    J3DPass::constantAlpha(true, 0);
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

    ImageEffectLocalUtil::capture(pScreenAlphaTexture2, 0, 1, GPU_CTF_A8, true, 0);

    J3DPass::writeColor(false);
    J3DPass::writeAlpha(true);
    J3DPass::constantAlpha(false, 0);
    J3DNativeView::setZScaleOffset(0.0f, 0.0f);
    // The mask: alpha 255 where the screen's alpha is under (or, by arg 0, over) 1, a pass of its own at half size, then
    // halved again and blurred, each into its own texture.
    const NebulaColor8 konst = {0, 0, 0, 1};
    const NebulaColor8 white = {255, 255, 255, 255};
    ImageEffectLocalUtil::setMode(_C ? ImageEffectLocalUtil::Mode_MaskAbove : ImageEffectLocalUtil::Mode_MaskBelow, konst,
                                  white);
    J3DNativeTarget::begin(mMask->mImage, mMask->getWidth(), mMask->getHeight(), 0, 0xFFFFFF);
    ImageEffectLocalUtil::drawTexture(pScreenAlphaTexture2, 2, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    J3DNativeTarget::end(mMask->mImage, NEBULA_TARGET_ALPHA);

    ImageEffectLocalUtil::setMode(ImageEffectLocalUtil::Mode_Alpha, konst, ::sEdgeColor);
    J3DNativeTarget::begin(pScreenAlphaTexture3->mImage, pScreenAlphaTexture3->getWidth(), pScreenAlphaTexture3->getHeight(), 0,
                           0xFFFFFF);
    ImageEffectLocalUtil::drawTexture(mMask, 4, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    J3DNativeTarget::end(pScreenAlphaTexture3->mImage, NEBULA_TARGET_ALPHA);

    J3DNativeTarget::begin(pScreenAlphaTexture4->mImage, pScreenAlphaTexture4->getWidth(), pScreenAlphaTexture4->getHeight(), 0,
                           0xFFFFFF);
    ImageEffectLocalUtil::blurTexture(pScreenAlphaTexture3, 4, 0, 5, 0.004f, 1.0f);
    J3DNativeTarget::end(pScreenAlphaTexture4->mImage, NEBULA_TARGET_ALPHA);

    setUpEdgeAndClearAlpha();
    ImageEffectLocalUtil::setMode(ImageEffectLocalUtil::Mode_Edge, konst, ::sEdgeColor);
    ImageEffectLocalUtil::drawTexture(pScreenAlphaTexture4, 1, 0, 255, ImageEffectLocalUtil::TexDrawType_2);
    setUpFillScreen();

    ::drawFillScreen(::sFillColor);

    MR::loadViewMtx();
    MR::loadProjectionMtx();

    J3DPass::writeColor(true);
    J3DPass::writeAlpha(true);
    J3DPass::constantAlpha(true, 0);
    J3DNativeView::setZScaleOffset(1.0f, 0.0f);
}

namespace MR {
    NameObj* createFallOutFieldDraw(const char*) {
        return MR::createSceneObj(SceneObj_FallOutFieldDraw);
    }
};  // namespace MR
