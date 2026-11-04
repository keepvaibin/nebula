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
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"

namespace {
    // static const s32 sBlurCount = _;
    // static const f32 sBlurRadius = _;
    // static const f32 sBlurIntensity = _;
    static Color8 sFillColor = Color8(0, 0, 0, 255);
    static Color8 sEdgeColor = Color8(0, 46, 200, 128);
};  // namespace

FallOutFieldDraw::FallOutFieldDraw(const char* pName) : NameObj(pName), _C() {
    MR::createClipAreaHolder();
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_FallOutFieldDraw);
    mDepthKey = ImageEffectLocalUtil::createFrameKey();
    mMask = JUTTexture::createTarget(0.5f, 0.5f);
    mMask->mWrapS = NEBULA_WRAP_CLAMP;
    mMask->mWrapT = NEBULA_WRAP_CLAMP;
    mMask->mMinType = NEBULA_FILTER_LINEAR;
    mMask->mMagType = NEBULA_FILTER_LINEAR;
    mBlurScratch = JUTTexture::createTarget(0.25f, 0.25f);
    mBlurScratch->mWrapS = NEBULA_WRAP_CLAMP;
    mBlurScratch->mWrapT = NEBULA_WRAP_CLAMP;
    mBlurScratch->mMinType = NEBULA_FILTER_LINEAR;
    mBlurScratch->mMagType = NEBULA_FILTER_LINEAR;
    mEdge = JUTTexture::createTarget(0.25f, 0.25f);
    mEdge->mWrapS = NEBULA_WRAP_CLAMP;
    mEdge->mWrapT = NEBULA_WRAP_CLAMP;
    mEdge->mMinType = NEBULA_FILTER_LINEAR;
    mEdge->mMagType = NEBULA_FILTER_LINEAR;
    mEdgeBlurred = JUTTexture::createTarget(0.25f, 0.25f);
    mEdgeBlurred->mWrapS = NEBULA_WRAP_CLAMP;
    mEdgeBlurred->mWrapT = NEBULA_WRAP_CLAMP;
    mEdgeBlurred->mMinType = NEBULA_FILTER_LINEAR;
    mEdgeBlurred->mMagType = NEBULA_FILTER_LINEAR;
}

namespace {
    // A flat colour over the screen where the stencil says, depth always passing and written.
    void sendFlat(const Color8& rColor, bool writeColor) {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_FLAT_COLOR, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, true,
                           NEBULA_CULL_NONE);
        material.color_write = writeColor ? 1 : 0;
        material.alpha_write = 0;
        NativeShader::setColor(material, 0, rColor.r, rColor.g, rColor.b, rColor.a);
        NativeShader::send(material);
    }
}  // namespace

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

    // What the field fills, by the volumes' stencil (arg 0: inside them, else outside), marked in the depth with the
    // nearest value there is, which nothing in the map reaches. The fill below writes the depth again.
    const u8 filled = _C ? NEBULA_STENCIL_IF_SET : NEBULA_STENCIL_IF_ZERO;
    sendFlat(::sFillColor, false);
    J3DNativeView::setZScaleOffset(0.0f, 0.0f);
    J3DPass::stencil(filled, NEBULA_STENCIL_KEEP);
    ImageEffectLocalUtil::sendTextureVertex();
    J3DPass::stencil(NEBULA_STENCIL_OFF, NEBULA_STENCIL_KEEP);

    // The mark as a half-size mask (arg 0: a pixel with any mark under it, else one with all marked), halved again and
    // blurred, each into its own texture, for the glow along the edge.
    J3DPass::writeColor(false);
    J3DPass::writeAlpha(true);
    J3DNativeTarget::beginOverFrame(mMask->mImage, mMask->mShareAcross, mMask->mShareDown, 0, nullptr, NEBULA_TARGET_RGBA, mDepthKey->mImage);
    NebulaNativeMaterial mask;
    NativeShader::init(mask, NEBULA_SHADER_FRAME_DEPTH, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false, NEBULA_CULL_NONE);
    mask.constants[0] = _C ? 1.0f : 2.0f;
    NativeShader::setTexture(mask, 0, mDepthKey);
    NativeShader::send(mask);
    ImageEffectLocalUtil::sendTextureVertex();
    J3DNativeTarget::end(mMask->mImage, NEBULA_TARGET_ALPHA);

    ImageEffectLocalUtil::setMode(ImageEffectLocalUtil::Mode_Alpha, ::sEdgeColor);
    J3DNativeTarget::begin(mEdge->mImage, mEdge->mShareAcross, mEdge->mShareDown, 0);
    ImageEffectLocalUtil::drawTexture(mMask, 255, ImageEffectLocalUtil::TexDrawType_0);
    J3DNativeTarget::end(mEdge->mImage, NEBULA_TARGET_ALPHA);

    ImageEffectLocalUtil::blurTexture(mEdge, mBlurScratch, mEdgeBlurred, 0.004f, NEBULA_TARGET_ALPHA, 5.0f);

    // The glow, then the fill over it and the depth behind everything.
    J3DPass::writeColor(true);
    J3DPass::writeAlpha(false);
    ImageEffectLocalUtil::setMode(ImageEffectLocalUtil::Mode_Edge, ::sEdgeColor);
    ImageEffectLocalUtil::drawTexture(mEdgeBlurred, 255, ImageEffectLocalUtil::TexDrawType_2);

    sendFlat(::sFillColor, true);
    J3DNativeView::setZScaleOffset(0.0f, 1.0f);
    J3DPass::stencil(filled, NEBULA_STENCIL_KEEP);
    ImageEffectLocalUtil::sendTextureVertex();
    J3DPass::stencil(NEBULA_STENCIL_OFF, NEBULA_STENCIL_KEEP);
    J3DNativeView::setZScaleOffset(1.0f, 0.0f);

    MR::clearStencil();
    MR::loadViewMtx();
    MR::loadProjectionMtx();

    J3DPass::writeColor(true);
    J3DPass::writeAlpha(true);
}

namespace MR {
    NameObj* createFallOutFieldDraw(const char*) {
        return MR::createSceneObj(SceneObj_FallOutFieldDraw);
    }
};  // namespace MR
