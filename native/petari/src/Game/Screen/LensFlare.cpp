#include "Game/System/NebulaFrameClock.hpp"
#include <nebula_module.h>
#include "Game/Util/MotionSignal.hpp"
#include "Game/Screen/LensFlare.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/BrightObj.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/TriggerChecker.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEW_NERVE(LensFlareModelNrvKill, LensFlareModel, Kill);
    NEW_NERVE(LensFlareModelNrvHide, LensFlareModel, Hide);
    NEW_NERVE(LensFlareModelNrvShow, LensFlareModel, Show);
    NEW_NERVE(LensFlareModelNrvFadeIn, LensFlareModel, FadeIn);
    NEW_NERVE(LensFlareModelNrvFadeOut, LensFlareModel, FadeOut);
};  // namespace

namespace {
    LensFlareDirector* getLensFlareDirector() {
        return MR::getSceneObj< LensFlareDirector >(SceneObj_LensFlareDirector);
    }

    // The most the sight's share moves in a frame: the models' own fade step (LensFlareModel::mFadeStep).
    const f32 cSightStep = 0.05f;

    // The frame the sight pass was last drawn in (Frame::number) and the target it wrote. Draw state alone: the step
    // reads none of it, and a frame that follows no sight pass starts its share from nothing.
    struct SightDrawn {
        u32 mFrame;
        u32 mTarget;
        bool mDrawn;
    };
    SightDrawn sSight = {0u, 0u, false};

    bool isModelShown(const LensFlareModel* pModel) {
        return !MR::isDead(pModel) && !MR::isHiddenModel(pModel);
    }
};  // namespace

LensFlareModel::LensFlareModel(const char* pName, const char* pArcName)
    : LiveActor(pName), mFlareStrength(), mFadeLevel(), mFadeStep(), mAreaChecker(new TriggerChecker()), mVisibleChecker(new TriggerChecker()) {
    lumacore::setClass(this, lumacore::kClassId< LensFlareModel >);
    initModelManagerWithAnm(pArcName, nullptr, false);

    MR::connectToScene3DModelFor2D(this);

    MR::invalidateClipping(this);

    initNerve(GET_NERVE_ANON(LensFlareModelNrvKill));

    kill();
}

void LensFlareModel::update(bool param1, bool param2) {
    mAreaChecker->update(param1);

    if (mAreaChecker->getOnTrigger()) {
        notifyInArea();
    } else if (mAreaChecker->getOffTrigger()) {
        setNerve(GET_NERVE_ANON(LensFlareModelNrvFadeOut));
    }

    mVisibleChecker->update(param2 && param1);

    if (mVisibleChecker->getOnTrigger()) {
        if (isNerve(GET_NERVE_ANON(LensFlareModelNrvHide))) {
            setNerve(GET_NERVE_ANON(LensFlareModelNrvShow));
        }
    } else if (mVisibleChecker->getOffTrigger() && (isNerve(GET_NERVE_ANON(LensFlareModelNrvShow)) || isNerve(GET_NERVE_ANON(LensFlareModelNrvFadeIn)))) {
        setNerve(GET_NERVE_ANON(LensFlareModelNrvHide));
    }
}

void LensFlareModel::exeKill() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        kill();
    }
}

void LensFlareModel::exeHide() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        mFadeLevel = 0.0f;
    }
}

void LensFlareModel::exeShow() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        mFadeLevel = 1.0f;
    }
}

void LensFlareModel::exeFadeIn() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
    }

    mFadeLevel += mFadeStep;

    if (mFadeLevel >= 1.0f) {
        mFadeLevel = 1.0f;
        setNerve(GET_NERVE_ANON(LensFlareModelNrvShow));
    }
}

void LensFlareModel::exeFadeOut() {
    mFadeLevel -= mFadeStep;

    if (mFadeLevel <= 0.0f) {
        mFadeLevel = 0.0f;
        setNerve(GET_NERVE_ANON(LensFlareModelNrvKill));
    }
}

void LensFlareModel::appear() {
    LiveActor::appear();
    appearAnim();
}

void LensFlareModel::control() {
    controlAnim();
}

void LensFlareModel::notifyInArea() {
    if (MR::isDead(this)) {
        appear();
    }

    if (mVisibleChecker->getLevel() > 0.0f) {
        setNerve(GET_NERVE_ANON(LensFlareModelNrvFadeIn));
    } else {
        setNerve(GET_NERVE_ANON(LensFlareModelNrvHide));
    }
}

LensFlareRing::LensFlareRing() : LensFlareModel("レンズフレアリング", "LensFlare"), mRingFrame() {
    lumacore::setClass(this, lumacore::kClassId< LensFlareRing >);
    mScale.x = 0.135f;
    mScale.y = 0.135f;
    mScale.z = 0.135f;
    mFadeStep = 0.05f;
}

void LensFlareRing::appearAnim() {
    MR::startBckWithInterpole(this, "LensFlare", 0);
}

void LensFlareRing::controlAnim() {
    MR::startBrk(this, "LensFlare");
    MR::setBrkRate(this, 0.0f);
    MR::setBrkFrame(this, (1.0f - mFlareStrength * mFadeLevel) * MR::getBrkCtrl(this)->getEnd());

    if (MR::isBckStopped(this)) {
        MR::startBckWithInterpole(this, "LensFlare", 0);
    }

    f32 frame = mRingFrame * MR::getBckCtrl(this)->getEnd();
    MR::setBckFrameAndStop(this, frame);
}

LensFlareGlow::LensFlareGlow() : LensFlareModel("グレア（円形）", "GlareGlow") {
    lumacore::setClass(this, lumacore::kClassId< LensFlareGlow >);
    mFadeStep = 0.05f;
}

void LensFlareGlow::appearAnim() {
    MR::startBtk(this, "GlareGlow");
}

void LensFlareGlow::controlAnim() {
    MR::startBrk(this, "GlareGlow");
    MR::setBrkRate(this, 0.0f);
    MR::setBrkFrame(this, (1.0f - mFlareStrength * mFadeLevel) * MR::getBrkCtrl(this)->getEnd());
}

LensFlareLine::LensFlareLine() : LensFlareModel("グレア（ライン）", "GlareLine") {
    lumacore::setClass(this, lumacore::kClassId< LensFlareLine >);
    mFadeStep = 0.05f;
}

void LensFlareLine::appearAnim() {
}

void LensFlareLine::controlAnim() {
    MR::startBrk(this, "GlareLine");
    MR::setBrkRate(this, 0.0f);
    MR::setBrkFrame(this, (1.0f - mFlareStrength * mFadeLevel) * MR::getBrkCtrl(this)->getEnd());
}

LensFlareDirector::LensFlareDirector()
    : NameObj("レンズフレア管理"), mRing(), mGlow(), mLine(), mBrightObjCount(), mSightObj(-1), mNowCenter(0.0f, 0.0f), mSightTargets(), mDepthKey(),
      mWithoutKey(), mWithKey() {
    lumacore::setClass(this, lumacore::kClassId< LensFlareDirector >);
}

LensFlareDirector::~LensFlareDirector() {
}

void LensFlareDirector::init(const TableIter& /*rIter*/) {
    mRing = new LensFlareRing();
    mGlow = new LensFlareGlow();
    mLine = new LensFlareLine();
    mSightTargets[0] = GameTexture::createPixelTarget(1, 1);
    mSightTargets[1] = GameTexture::createPixelTarget(1, 1);
    mDepthKey = ImageEffectLocalUtil::createFrameKey();
    mWithoutKey = ImageEffectLocalUtil::createFrameKey();
    mWithKey = ImageEffectLocalUtil::createFrameKey();

    MR::connectToSceneMapObjMovement(this);
}

void LensFlareDirector::movement() {
    s32 area = checkArea();
    controlFlare(area, checkBrightObj(area != 0));
}

void LensFlareDirector::pauseOff() {
    MR::requestMovementOn(this);
    MR::requestMovementOn(mRing);
    MR::requestMovementOn(mGlow);
    MR::requestMovementOn(mLine);
}

s32 LensFlareDirector::checkArea() {
    s32 area = 0;
    AreaObj* pAreaObj = MR::getAreaObj("LensFlareArea", *MR::getPlayerPos());

    if (pAreaObj != nullptr) {
        area = MR::getAreaObjArg(pAreaObj, 0);

        if (area == -1) {
            area = 0xFFFF;
        }
    }

    return area;
}

// The first object that gives light with a sample on the screen, from the step's camera alone: whether something stands
// in front of it is the sight's, in the picture.
bool LensFlareDirector::checkBrightObj(bool inArea) {
    if (!inArea) {
        return false;
    }

    BrightSample samples[BrightObjBase::cSampleCount];
    for (u32 i = 0; i < mBrightObjCount; i++) {
        BrightObjBase* pObj = mBrightObjArray[i];
        if (pObj->isShining() && pObj->calcSamples(samples) != 0) {
            mSightObj = static_cast< s32 >(i);
            pObj->getNowCenter(&mNowCenter);
            return true;
        }
    }

    return false;
}

void LensFlareDirector::controlFlare(s32 area, bool hasBrightObj) {
    mRing->update(area >> 1 & 1, hasBrightObj);
    mGlow->update(area >> 2 & 1, hasBrightObj);
    mLine->update(area >> 3 & 1, hasBrightObj);

    if (!hasBrightObj || area == 0) {
        return;
    }

    const Vec2f position(mNowCenter);
    Vec3f flarePos(position.x, -position.y, 0.0f);
    mRing->mPosition.set(flarePos);
    mGlow->mPosition.set(flarePos);
    mLine->mPosition.set(flarePos);

    Vec2f screenCenter(MR::getScreenWidth() / 2.0f, static_cast< s32 >(NebulaFrameClock::cScreenLines) / 2.0f);

    f32 screenRadius = screenCenter.length();
    Vec2f offset(screenCenter - position);
    f32 distanceRate = offset.length() / screenRadius;

    Vec3f rotation(0.0f, 0.0f, 0.0f);
    Vec2f direction(offset.x, offset.y);

    if (MR::isNearZero(direction)) {
        rotation.z = 0.0f;
    } else {
        MR::normalize(&direction);
        rotation.z = MR::calcRotateY(direction.x, direction.y);
    }

    mRing->mRotation.set(rotation);
    f32 ringFrame = distanceRate;
    if (ringFrame > 1.0f) {
        ringFrame = 1.0f;
    }

    mRing->mRingFrame = ringFrame;
    mRing->mFlareStrength = MR::clamp(1.0f - distanceRate, 0.0f, 1.0f);
    mGlow->mFlareStrength = MR::clamp(1.0f - distanceRate, 0.0f, 1.0f);
    mLine->mFlareStrength = MR::clamp(1.0f - distanceRate, 0.0f, 1.0f);
}

bool LensFlareDirector::isFlareShown() const {
    return ::isModelShown(mRing) || ::isModelShown(mGlow) || ::isModelShown(mLine);
}

// The share of the object's samples nothing drawn stands in front of, read from the scene's depth into the sight target
// of this frame and eased from the other's (flare_sight.hlsl).
void LensFlareDirector::drawSight() const {
    if (mSightObj < 0 || !isFlareShown()) {
        return;
    }

    const u32 frame = Frame::number();
    const bool hasLast = sSight.mDrawn && sSight.mFrame + 1u == frame;
    const u32 target = hasLast ? 1u - sSight.mTarget : 0u;

    BrightSample samples[BrightObjBase::cSampleCount];
    const BrightObjBase* pObj = mBrightObjArray[mSightObj];
    if (pObj->isShining()) {
        pObj->calcSamples(samples);
    } else {
        for (u32 i = 0; i < BrightObjBase::cSampleCount; i++) {
            samples[i].mOnScreen = false;
        }
    }

    ImageEffectLocalUtil::setupDrawTexture();
    GameTexture* pTarget = mSightTargets[target];
    Frame::targetBeginPixelsOverFrame(pTarget->image(), 1, 1, 0, nullptr, NEBULA_TARGET_RGBA, mDepthKey->image());
    SiteMaterial material;
    material.init(NEBULA_SHADER_FLARE_SIGHT, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false, NEBULA_CULL_NONE);
    material.setTexture(0, mDepthKey);
    if (hasLast) {
        material.setTexture(1, mSightTargets[sSight.mTarget]);
    }
    material.constants[0] = hasLast ? 1.0f : 0.0f;
    material.constants[1] = ::cSightStep;
    for (u32 i = 0; i < BrightObjBase::cSampleCount; i++) {
        material.constants[4 + i * 4] = samples[i].mFrameX;
        material.constants[5 + i * 4] = samples[i].mFrameY;
        material.constants[6 + i * 4] = samples[i].mDepth;
        material.constants[7 + i * 4] = samples[i].mOnScreen ? 1.0f : 0.0f;
    }
    material.mState.alpha_write = 0;
    ImageEffectLocalUtil::drawQuad(material);
    Frame::targetEnd(pTarget->image(), NEBULA_TARGET_RED);
    MR::loadProjectionMtx();
    MR::loadViewMtx();

    sSight.mFrame = frame;
    sSight.mTarget = target;
    sSight.mDrawn = true;
}

// The frame before the flare's models are drawn.
bool LensFlareDirector::beginFlareDraw() const {
    if (!isFlareShown()) {
        return false;
    }

    mWithoutKey->copyFrame();
    return true;
}

// The frame with the models and without them, mixed by this frame's share (flare_blend.hlsl); none without a sight pass.
void LensFlareDirector::endFlareDraw() const {
    mWithKey->copyFrame();
    const bool hasSight = sSight.mDrawn && sSight.mFrame == Frame::number();

    ImageEffectLocalUtil::setupDrawTexture();
    SiteMaterial material;
    material.init(NEBULA_SHADER_FLARE_BLEND, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false, NEBULA_CULL_NONE);
    material.setTexture(0, mWithoutKey);
    material.setTexture(1, mWithKey);
    if (hasSight) {
        material.setTexture(2, mSightTargets[sSight.mTarget]);
    }
    material.constants[0] = hasSight ? 1.0f : 0.0f;
    ImageEffectLocalUtil::drawQuad(material);
    MR::drawInitFor2DModel();
}

namespace MR {
    void addBrightObj(BrightObjBase* pBrightObj) {
        if (!MR::isExistSceneObj(SceneObj_LensFlareDirector)) {
            MR::createSceneObj(SceneObj_LensFlareDirector);
        }

        LensFlareDirector* pDirector = ::getLensFlareDirector();
        if (pDirector->mBrightObjCount == static_cast< u32 >(pDirector->mBrightObjArray.size())) {
            pDirector->mBrightObjArray.grow();
        }

        pDirector->mBrightObjArray[pDirector->mBrightObjCount++] = pBrightObj;
    }

    void drawLensFlareSight() {
        if (MR::isExistSceneObj(SceneObj_LensFlareDirector)) {
            ::getLensFlareDirector()->drawSight();
        }
    }

    bool beginLensFlareDraw() {
        return MR::isExistSceneObj(SceneObj_LensFlareDirector) && ::getLensFlareDirector()->beginFlareDraw();
    }

    void endLensFlareDraw() {
        ::getLensFlareDirector()->endFlareDraw();
    }

};  // namespace MR

