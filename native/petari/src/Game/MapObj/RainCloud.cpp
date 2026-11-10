#include "Game/MapObj/RainCloud.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapPartsRailMover.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRainCylinder_f1, "updateRainCylinder.f1", 0.1f);

namespace {
    NEBULA_TUNABLE(f32, sNoShadowDropLengthMax, 2000.0f);
    NEBULA_TUNABLE(f32, sRainCylinderBaseLength, 1000.0f);
    NEBULA_TUNABLE(f32, sSwitchEffectBorder, 1500.0f);
    NEBULA_TUNABLE(s32, sThunderStep, 140);
};  // namespace

namespace NrvRainCloud {
    NEW_NERVE(RainCloudNrvAppear, RainCloud, Appear);
    NEW_NERVE(RainCloudNrvWait, RainCloud, Wait);
    NEW_NERVE(RainCloudNrvDisappear, RainCloud, Disappear);
    NEW_NERVE(RainCloudNrvEnd, RainCloud, End);
    NEW_NERVE(RainCloudNrvSoftTouch, RainCloud, SoftTouch);
    NEW_NERVE(RainCloudNrvHardTouch, RainCloud, HardTouch);
};  // namespace NrvRainCloud

RainCloud::RainCloud(const char* pName)
    : LiveActor(pName), mCloudCylinder(), mRailMover(), mLodCtrl(), mClippingCenter(0.0f, 0.0f, 0.0f), mRainLength(), mShadowDropLength(-1.0f),
      mIsOnPlayer(), mNoRain() {
    mRainCylinderMtx.identity();
    mSplashMtx.identity();
}

void RainCloud::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("RainCloud", nullptr, false);
    MR::getPlacementArg3NoInit(rIter, &mNoRain);

    if (!mNoRain) {
        mRainCylinderMtx.set(getBaseMtx());
        mCloudCylinder = new ModelObj("雨雲雨範囲", "RainCloudCylinder", mRainCylinderMtx, -2, -2, -2, false);
        mCloudCylinder->initWithoutIter();
        MR::invalidateClipping(mCloudCylinder);
    }

    MR::connectToSceneCollisionMapObj(this);

    if (!mNoRain) {
        initHitSensor(2);
        MR::addBodyMessageSensorMapObj(this);
        MR::addHitSensorCallbackMapObjSimple(this, "drop_water", 4, cNum_init_f1);
    } else {
        initHitSensor(1);
        MR::addBodyMessageSensorMapObj(this);
    }

    MR::initCollisionParts(this, "RainCloud", getSensor("body"), nullptr);
    initEffectKeeper(0, nullptr);

    if (!mNoRain) {
        MR::setEffectHostMtx(this, "Splash", mSplashMtx);
    }

    initSound(4, false);

    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
        mRailMover = new MapPartsRailMover(this);
        mRailMover->init(rIter);
        mRailMover->start();
    }

    mLodCtrl = MR::createLodCtrlPlanet(this, rIter, -1.0f);
    MR::initShadowVolumeFlatModel(this, "RainCloudVolume", MR::getJointMtx(this, "Shadow"));
    MR::getPlacementArg0NoInit(rIter, &mShadowDropLength);

    f32 dropLength;
    if ((dropLength = mShadowDropLength) != -1.0f) {
        mRainLength = dropLength;
        MR::setShadowDropLength(this, nullptr, dropLength);
        MR::setShadowVolumeEndDropOffset(this, nullptr, cNum_init_f2);
        MR::excludeCalcShadowToMyCollision(this, nullptr);
        MR::onShadowVolumeCutDropLength(this, nullptr);
        MR::onCalcShadow(this, nullptr);
    } else {
        mShadowDropLength = dropLength = ::sNoShadowDropLengthMax;
        MR::invalidateShadow(this, nullptr);
    }

    Vec3f clippingCenter;
    clippingCenter = mPosition;
    f32 boundRadius;
    MR::calcModelBoundingRadius(&boundRadius, this);
    boundRadius += cNum_init_f3 * mRainLength;

    if (mRailMover != nullptr) {
        f32 railRadius;
        MR::calcRailClippingInfo(&clippingCenter, &railRadius, this, cNum_init_f4, cNum_init_f5);
        boundRadius += railRadius;
    }

    mClippingCenter.scaleAdd(cNum_init_f6 * mRainLength, mGravity, mPosition);
    MR::setClippingTypeSphere(this, boundRadius, &mClippingCenter);
    MR::setGroupClipping(this, rIter, 16);
    MR::joinToGroupArray(this, rIter, "雲集団", 16);
    MR::startBck(this, "Wait");

    if (!mNoRain) {
        MR::startBpk(this, "Rain");
    } else {
        MR::startBpk(this, "Fine");
    }

    initNerve(GET_NERVE(RainCloud, RainCloudNrvWait));
    makeActorAppeared();
}

void RainCloud::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Appear");
        MR::startBpk(this, "Appear");
        MR::validateCollisionParts(this);
        // A cloud with no rain (arg 3) has no cylinder.
        if (mCloudCylinder != nullptr) {
            mCloudCylinder->appear();
        }

        MR::startSound(this, "SE_OJ_RAIN_CLOUD_APPEAR");
    }

    if (MR::isStep(this, ::sThunderStep)) {
        MR::startSound(this, "SE_OJ_RAIN_CLOUD_THUNDER");
    }

    if (MR::isBckStopped(this)) {
        MR::validateHitSensors(this);
        setNerve(GET_NERVE(RainCloud, RainCloudNrvWait));
    }
}

void RainCloud::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Wait");
    }

    if (!MR::isOnPlayer(this) && mIsOnPlayer) {
        mIsOnPlayer = false;
        setNerve(GET_NERVE(RainCloud, RainCloudNrvSoftTouch));
    }
}

void RainCloud::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Disappear");
        MR::invalidateHitSensors(this);
        MR::invalidateCollisionParts(this);
        MR::startSound(this, "SE_OJ_RAIN_CLOUD_DISAPPEAR");
    }

    if (MR::isBckStopped(this)) {
        if (mCloudCylinder != nullptr) {
            mCloudCylinder->kill();
        }

        mIsOnPlayer = false;
    }

    if (MR::isMapPartsRailMovePassedStartPointRepeat(mRailMover)) {
        setNerve(GET_NERVE(RainCloud, RainCloudNrvEnd));
    }
}

void RainCloud::exeEnd() {
    if (MR::isBckStopped(this) && isNextStartOK()) {
        if (mCloudCylinder != nullptr) {
            mCloudCylinder->kill();
        }

        setNerve(GET_NERVE(RainCloud, RainCloudNrvAppear));
    }
}

void RainCloud::exeSoftTouch() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "OnBee");
    }

    if (MR::isBckStopped(this)) {
        mIsOnPlayer = MR::isOnPlayer(this);
        setNerve(GET_NERVE(RainCloud, RainCloudNrvWait));
    }
}

void RainCloud::exeHardTouch() {
    if (MR::isFirstStep(this)) {
        MR::invalidateCollisionParts(this);
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::shakeCameraNormal();
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::validateCollisionParts(this);
        setNerve(GET_NERVE(RainCloud, RainCloudNrvWait));
    }
}

void RainCloud::control() {
    mLodCtrl->update();

    if (mRailMover != nullptr && !isNerve(GET_NERVE(RainCloud, RainCloudNrvEnd))) {
        mRailMover->movement();
        mPosition.set(mRailMover->mStartPos);
        if (MR::isMapPartsRailMovePassedEndPointRepeat(mRailMover)) {
            setNerve(GET_NERVE(RainCloud, RainCloudNrvDisappear));
            return;
        }
    }

    if (MR::isShadowProjected(this, nullptr)) {
        mRainLength = MR::getShadowProjectionLength(this, nullptr);
    } else {
        mRainLength = mShadowDropLength;
    }

    if (!mNoRain) {
        switchEffect();
        updateRainCylinder();
    }
}

void RainCloud::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        if (MR::isPlayerElementModeBee()) {
            MR::curePlayerElementMode();
            MR::tryRumblePadStrong(this, PLAYER_1);
        }

        MR::sendArbitraryMsg(ACTMES_PUDDLE_TOUCH_GROUND, pReceiver, pSender);
    }
}

bool RainCloud::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_END_DEMO && !mNoRain) {
        switchEffect();
        return true;
    }

    bool isChanging = isNerve(GET_NERVE(RainCloud, RainCloudNrvAppear)) || isNerve(GET_NERVE(RainCloud, RainCloudNrvDisappear)) ||
                      isNerve(GET_NERVE(RainCloud, RainCloudNrvEnd));
    if (isChanging) {
        return false;
    }

    if (isNerve(GET_NERVE(RainCloud, RainCloudNrvHardTouch))) {
        return false;
    }

    if (isNerve(GET_NERVE(RainCloud, RainCloudNrvSoftTouch))) {
        return false;
    }

    if (MR::isMsgFloorTouch(msg)) {
        if (!MR::isPlayerElementModeBee()) {
            setNerve(GET_NERVE(RainCloud, RainCloudNrvHardTouch));
        } else if (!mIsOnPlayer) {
            setNerve(GET_NERVE(RainCloud, RainCloudNrvSoftTouch));
        }

        return true;
    }

    return false;
}

void RainCloud::updateHitSensor(HitSensor* pSensor) {
    f32 radius = pSensor->mRadius;
    Vec3f start;
    start.scaleAdd(radius, mGravity, mPosition);
    Vec3f end;
    end.scaleAdd(mRainLength - radius, mGravity, mPosition);

    MR::calcPerpendicFootToLineInside(&pSensor->mPosition, *MR::getPlayerPos(), start, end);
}

void RainCloud::startClipped() {
    MR::deleteEffectAll(this);
    LiveActor::startClipped();

    if (mCloudCylinder != nullptr) {
        mCloudCylinder->startClipped();
    }
}

void RainCloud::endClipped() {
    LiveActor::endClipped();

    if (mCloudCylinder != nullptr) {
        mCloudCylinder->endClipped();
    }

    if (!mNoRain && !MR::isDemoActive()) {
        switchEffect();
    }
}

void RainCloud::switchEffect() {
    bool isChanging = isNerve(GET_NERVE(RainCloud, RainCloudNrvAppear)) || isNerve(GET_NERVE(RainCloud, RainCloudNrvDisappear)) ||
                      isNerve(GET_NERVE(RainCloud, RainCloudNrvEnd));

    if (isChanging) {
        if (MR::isEffectValid(this, "Splash")) {
            MR::deleteEffect(this, "Splash");
        }

        if (MR::isEffectValid(this, "Line20")) {
            MR::deleteEffect(this, "Line20");
        }

        if (MR::isEffectValid(this, "Line10")) {
            MR::deleteEffect(this, "Line10");
        }
    } else {
        bool isLongRain;
        if (MR::isShadowProjected(this, nullptr)) {
            isLongRain = mRainLength > ::sSwitchEffectBorder;
            Vec3f projectionPos;
            MR::getShadowProjectionPos(this, nullptr, &projectionPos);
            Vec3f projectionNormal;
            MR::getShadowProjectionNormal(this, nullptr, &projectionNormal);
            MR::makeMtxUpNoSupportPos(&mSplashMtx, projectionNormal, projectionPos);

            if (!MR::isEffectValid(this, "Splash")) {
                MR::emitEffect(this, "Splash");
            }
        } else {
            isLongRain = true;
            MR::deleteEffect(this, "Splash");
        }

        if (isLongRain && !MR::isEffectValid(this, "Line20")) {
            MR::deleteEffect(this, "Line10");
            MR::emitEffect(this, "Line20");
        } else if (!isLongRain && !MR::isEffectValid(this, "Line10")) {
            MR::deleteEffect(this, "Line20");
            MR::emitEffect(this, "Line10");
        }
    }
}

void RainCloud::updateRainCylinder() {
    if (MR::isDead(mCloudCylinder)) {
        return;
    }

    mRainCylinderMtx.set(MR::getJointMtx(this, "Shadow"));
    f32 lengthScale = mRainLength / ::sRainCylinderBaseLength;
    f32 widthScale = 1.0f;
    if (isNerve(GET_NERVE(RainCloud, RainCloudNrvAppear)) || isNerve(GET_NERVE(RainCloud, RainCloudNrvDisappear))) {
        if (MR::isBckPlaying(this, "Appear")) {
            widthScale = MR::getBckFrame(this) / MR::getBckFrameMax(this);
        } else if (MR::isBckPlaying(this, "Disappear")) {
            widthScale = 1.0f - MR::getBckFrame(this) / MR::getBckFrameMax(this);
        }

        widthScale = MR::clamp(widthScale, cNum_updateRainCylinder_f1, 1.0f);
    }

    MR::preScaleMtx(mRainCylinderMtx, Vec3f(widthScale, lengthScale, widthScale));
    MR::startLevelSound(this, "SE_OJ_LV_FOREST_RAIN");
}

bool RainCloud::isNextStartOK() const {
    if (MR::getGroupFromArray(this) == nullptr) {
        return true;
    }

    LiveActor* pCloud;
    for (s32 i = 0; i < MR::getGroupFromArray(this)->getObjNum(); i++) {
        pCloud = MR::getGroupFromArray(this)->getActor(i);
        bool isReady = pCloud->isNerve(GET_NERVE(RainCloud, RainCloudNrvEnd)) ||
                       (pCloud->isNerve(GET_NERVE(RainCloud, RainCloudNrvAppear)) && MR::isFirstStep(pCloud));
        if (!isReady) {
            return false;
        }
    }

    return true;
}
