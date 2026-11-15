#include "Game/System/NebulaFrameClock.hpp"
#include <nebula_module.h>
#include "Game/Util/MotionSignal.hpp"
#include "Game/Screen/LensFlare.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/BrightObj.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
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
    : NameObj("レンズフレア管理"), mRing(), mGlow(), mLine(), mBrightObjCount(), mAsker(Frame::newProbeAsker()), mBrightCenter(0.0f, 0.0f), mBrightLevel(), mObjCenter(0.0f, 0.0f), mNowCenter(0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< LensFlareDirector >);
}

LensFlareDirector::~LensFlareDirector() {
}

void LensFlareDirector::init(const TableIter& /*rIter*/) {
    mRing = new LensFlareRing();
    mGlow = new LensFlareGlow();
    mLine = new LensFlareLine();

    MR::connectToSceneMapObjMovement(this);
}

void LensFlareDirector::movement() {
    // The newest answers go to the objects right before the flare reads them: answered in the draw, they waited a frame.
    readAnswers();
    s32 area = checkArea();
    controlFlare(area, checkBrightObj(area != 0));

}

void LensFlareDirector::pauseOff() {
    MR::requestMovementOn(this);
    MR::requestMovementOn(mRing);
    MR::requestMovementOn(mGlow);
    MR::requestMovementOn(mLine);
}

void LensFlareDirector::reserveProbes() {
    const u32 count = mBrightObjCount * BrightObjBase::cProbeCount;
    if (count > mProbeCapacity) {
        delete[] mProbes;
        mProbes = new NebulaScreenProbe[count];
        mProbeCapacity = count;
    }
}

// What the last finished probes saw, for each object.
void LensFlareDirector::readAnswers() {
    reserveProbes();
    const u32 count = mBrightObjCount * BrightObjBase::cProbeCount;
    NebulaScreenProbe* const probes = mProbes;
    const u32 done = Frame::screenProbeResults(BrightObjBase::cReadbackChannel, mAsker, probes, count);
    for (u32 i = 0; i < mBrightObjCount && (i + 1) * BrightObjBase::cProbeCount <= done; i++) {
        const NebulaScreenProbe* pProbes = &probes[i * BrightObjBase::cProbeCount];
        if (pProbes->x != BrightObjBase::cNoProbes) {
            mBrightObjArray[i]->setResult(pProbes);
        }
    }
}

// This picture's points, asked with the camera it is drawn with.
void LensFlareDirector::setDrawSyncToken() {
    reserveProbes();
    const u32 count = mBrightObjCount * BrightObjBase::cProbeCount;
    NebulaScreenProbe* const probes = mProbes;
    for (u32 i = 0; i < mBrightObjCount; i++) {
        mBrightObjArray[i]->calcBrightInfo(&probes[i * BrightObjBase::cProbeCount]);
    }
    // BrightObjBase::setResult only asks whether each point's depth is at or behind the point.
    Frame::probeScreen(BrightObjBase::cReadbackChannel, mAsker, probes, count, true);
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

bool LensFlareDirector::checkBrightObj(bool b1) {
    bool ret = false;
    if (b1 && mBrightObjCount != 0) {

        s32 index = -1;
        for (u32 i = 0; i < mBrightObjCount; i++) {
            if (mBrightObjArray[i]->getBright() > 0.0f) {
                index = i;
                break;
            }
        }

        if (index >= 0) {
            ret = true;

            mBrightCenter.set(*mBrightObjArray[index]->getBrightCenter());
            mBrightLevel = mBrightObjArray[index]->getBright();
            mObjCenter.set(*mBrightObjArray[index]->getCenter());
            mBrightObjArray[index]->getNowCenter(&mNowCenter);
        }
    }

    return ret;
}

void LensFlareDirector::controlFlare(s32 area, bool hasBrightObj) {
    mRing->update(area >> 1 & 1, hasBrightObj);
    mGlow->update(area >> 2 & 1, hasBrightObj);
    mLine->update(area >> 3 & 1, hasBrightObj);

    if (!hasBrightObj || area == 0) {
        return;
    }

    Vec2f position(mBrightCenter - mObjCenter);
    position += mNowCenter;
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
    mRing->mFlareStrength = MR::clamp((1.0f - distanceRate) * mBrightLevel, 0.0f, 1.0f);
    mGlow->mFlareStrength = MR::clamp((1.0f - distanceRate) * mBrightLevel, 0.0f, 1.0f);
    mLine->mFlareStrength = MR::clamp((1.0f - distanceRate) * mBrightLevel, 0.0f, 1.0f);
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

    void setLensFlareDrawSyncToken() {
        if (MR::isExistSceneObj(SceneObj_LensFlareDirector)) {
            ::getLensFlareDirector()->setDrawSyncToken();
        }
    }

};  // namespace MR

