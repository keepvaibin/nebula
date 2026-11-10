#include <nebula_game_api.h>
#include "Game/Render/FrameList.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/MapObj/FallOutFieldDraw.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/Material.hpp"

#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/MapObj/ClipAreaHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_FallOutFieldDraw_f1, "FallOutFieldDraw.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_FallOutFieldDraw_f2, "FallOutFieldDraw.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_FallOutFieldDraw_f3, "FallOutFieldDraw.f3", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_FallOutFieldDraw_f4, "FallOutFieldDraw.f4", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_FallOutFieldDraw_f5, "FallOutFieldDraw.f5", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_FallOutFieldDraw_f6, "FallOutFieldDraw.f6", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_FallOutFieldDraw_f7, "FallOutFieldDraw.f7", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_FallOutFieldDraw_f8, "FallOutFieldDraw.f8", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f2, "draw.f2", 0.004f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f3, "draw.f3", 5.0f);

namespace {
    static Color8 sFillColor = Color8(0, 0, 0, 255);
    static Color8 sEdgeColor = Color8(0, 46, 200, 128);
};  // namespace

FallOutFieldDraw::FallOutFieldDraw(const char* pName) : NameObj(pName), mFillInsideVolume() {
    MR::createClipAreaHolder();
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_FallOutFieldDraw);
    mDepthKey = ImageEffectLocalUtil::createFrameKey();
    mMask = GameTexture::createTarget(cNum_FallOutFieldDraw_f1, cNum_FallOutFieldDraw_f2);
    mMask->setSampling(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
    mBlurScratch = GameTexture::createTarget(cNum_FallOutFieldDraw_f3, cNum_FallOutFieldDraw_f4);
    mBlurScratch->setSampling(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
    mEdge = GameTexture::createTarget(cNum_FallOutFieldDraw_f5, cNum_FallOutFieldDraw_f6);
    mEdge->setSampling(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
    mEdgeBlurred = GameTexture::createTarget(cNum_FallOutFieldDraw_f7, cNum_FallOutFieldDraw_f8);
    mEdgeBlurred->setSampling(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
}

namespace {
    // A flat colour over the screen where the stencil says, depth always passing and written.
    SiteMaterial flatMaterial(const Color8& rColor, bool writeColor, u8 stencil) {
        SiteMaterial material;
        material.init(NEBULA_SHADER_FLAT_COLOR, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, true,
                           NEBULA_CULL_NONE);
        material.mState.color_write = writeColor ? 1 : 0;
        material.mState.alpha_write = 0;
        material.setColor(0, rColor.r, rColor.g, rColor.b, rColor.a);
        material.setStencil(stencil, NEBULA_STENCIL_KEEP);
        return material;
    }
}  // namespace

void FallOutFieldDraw::init(const TableIter& rIter) {
    if (!MR::isValidInfo(rIter)) {
        return;
    }

    bool fillInside;
    MR::getPlacementArg0WithInit(rIter, &fillInside);
    mFillInsideVolume = fillInside;

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
    const u8 filled = mFillInsideVolume ? NEBULA_STENCIL_IF_SET : NEBULA_STENCIL_IF_ZERO;
    Frame::setZScaleOffset(0.0f, 0.0f);
    ImageEffectLocalUtil::drawQuad(flatMaterial(::sFillColor, false, filled));

    // The mark as a half-size mask (arg 0: a pixel with any mark under it, else one with all marked), halved again and
    // blurred, each into its own texture, for the glow along the edge.
    ImageEffectLocalUtil::setWrites(false, true);
    Frame::targetBeginOverFrame(mMask->image(), mMask->shareAcross(), mMask->shareDown(), 0, nullptr, NEBULA_TARGET_RGBA, mDepthKey->image());
    SiteMaterial mask;
    mask.init(NEBULA_SHADER_FRAME_DEPTH, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false, NEBULA_CULL_NONE);
    mask.mState.color_write = 0;
    mask.mState.alpha_write = 1;
    mask.constants[0] = mFillInsideVolume ? 1.0f : cNum_draw_f1;
    mask.setTexture(0, mDepthKey);
    ImageEffectLocalUtil::drawQuad(mask);
    Frame::targetEnd(mMask->image(), NEBULA_TARGET_ALPHA);

    ImageEffectLocalUtil::setMode(ImageEffectLocalUtil::Mode_Alpha, ::sEdgeColor);
    Frame::targetBegin(mEdge->image(), mEdge->shareAcross(), mEdge->shareDown(), 0);
    ImageEffectLocalUtil::drawTexture(mMask, 255, ImageEffectLocalUtil::TexDrawType_0);
    Frame::targetEnd(mEdge->image(), NEBULA_TARGET_ALPHA);

    ImageEffectLocalUtil::blurTexture(mEdge, mBlurScratch, mEdgeBlurred, cNum_draw_f2, NEBULA_TARGET_ALPHA, cNum_draw_f3);

    // The glow, then the fill over it and the depth behind everything.
    ImageEffectLocalUtil::setWrites(true, false);
    ImageEffectLocalUtil::setMode(ImageEffectLocalUtil::Mode_Edge, ::sEdgeColor);
    ImageEffectLocalUtil::drawTexture(mEdgeBlurred, 255, ImageEffectLocalUtil::TexDrawType_2);

    Frame::setZScaleOffset(0.0f, 1.0f);
    ImageEffectLocalUtil::drawQuad(flatMaterial(::sFillColor, true, filled));
    Frame::setZScaleOffset(1.0f, 0.0f);

    MR::clearStencil();
    MR::loadViewMtx();
    MR::loadProjectionMtx();
}

namespace MR {
    NameObj* createFallOutFieldDraw(const char*) {
        return MR::createSceneObj(SceneObj_FallOutFieldDraw);
    }
};  // namespace MR
