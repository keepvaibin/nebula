#include "Game/MapObj/SuperSpinDriver.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/SpinDriverCamera.hpp"
#include "Game/MapObj/SpinDriverOperateRing.hpp"
#include "Game/MapObj/SpinDriverPathDrawer.hpp"
#include "Game/MapObj/SpinDriverShootPath.hpp"
#include "Game/MapObj/SpinDriverUtil.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sCanBindTime = NebulaFrameClock::stepsOfMilliseconds(1500);
    NEBULA_TUNABLE(f32, sSensorRadius, 300.0f);
    NEBULA_TUNABLE(f32, sClippingRadius, 300.0f);
    NEBULA_TUNABLE(f32, sPullPlayerDistance, 240.0f);
    NEBULA_TUNABLE(f32, sFarPlayerDistance, 350.0f);
    // The player's carried-in speed is capped at bind.
    NEBULA_TUNABLE(f32, sBindSpeedMax, 40.0f);
    NEBULA_TUNABLE(s32, sCaptureFrame, 60);
    NEBULA_TUNABLE(f32, sCaptureEndDistance, 15.0f);
    NEBULA_TUNABLE(f32, sCaptureSpinAccel, 0.004f);
    NEBULA_TUNABLE(f32, sCapturePullRate, 1.5f);
    NEBULA_TUNABLE(f32, sCapturePullDistance, 120.0f);
    NEBULA_TUNABLE(f32, sCaptureDamp, 0.8f);
    NEBULA_TUNABLE(s32, sShootStartFrame, 15);
    NEBULA_TUNABLE(s32, sShootWaitFrame, 45);
    NEBULA_TUNABLE(f32, sShootStartSpinDamp, 0.8f);
    // A rush cancel early in the shot is refused.
    NEBULA_TUNABLE(s32, sShootNoCancelStep, 40);
    NEBULA_TUNABLE(s32, sCoolDownFrame, 60);
    const s32 sCoolDownWaitStep = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, sSpinDamp, 0.985f);
    NEBULA_TUNABLE(f32, sSwingSpinAccel, 0.1f);
    NEBULA_TUNABLE(f32, sSwingSpinMax, 0.23f);
    NEBULA_TUNABLE(f32, sHeadTurnRate, 0.4f);
    NEBULA_TUNABLE(f32, sOperateDirWeight, 0.8f);
    NEBULA_TUNABLE(f32, sOperateDropOffset, -75.0f);
    NEBULA_TUNABLE(f32, sPathDirectionDelta, 0.01f);
    // The last frames of the flight are the fall; the fly loop starts at most this far in.
    NEBULA_TUNABLE(s32, sFallFrame, 20);
    NEBULA_TUNABLE(f32, sFlyLoopStartRate, 0.2f);
    NEBULA_TUNABLE(s32, sFlyLoopStartMax, 90);
    // Swing turns per loop step, whole turns only.
    NEBULA_TUNABLE(f32, sSwingTurnPerStep, 0.05f);
    NEBULA_TUNABLE(s32, sDefaultSpaceFlyStartFrame, 10);
    NEBULA_TUNABLE(s32, sDefaultSpaceFlyEndFrame, 50);
    const char* const cDemoNameAppear = "出現";
    const char* const cLightEffect = "SuperSpinDriverLight";
    const char* const cGlowEffect = "EndGlow";
}  // namespace

namespace NrvSuperSpinDriver {
    NEW_NERVE(SuperSpinDriverNrvTryDemo, SuperSpinDriver, TryDemo);
    NEW_NERVE(SuperSpinDriverNrvEmptyNonActive, SuperSpinDriver, EmptyNonActive);
    NEW_NERVE(SuperSpinDriverNrvEmptyAppear, SuperSpinDriver, EmptyAppear);
    NEW_NERVE(SuperSpinDriverNrvEmptyWait, SuperSpinDriver, EmptyWait);
    NEW_NERVE(SuperSpinDriverNrvNonActive, SuperSpinDriver, NonActive);
    NEW_NERVE(SuperSpinDriverNrvAppear, SuperSpinDriver, Appear);
    NEW_NERVE(SuperSpinDriverNrvWait, SuperSpinDriver, Wait);
    NEW_NERVE(SuperSpinDriverNrvCapture, SuperSpinDriver, Capture);
    NEW_NERVE(SuperSpinDriverNrvShootStart, SuperSpinDriver, ShootStart);
    NEW_NERVE_ONEND(SuperSpinDriverNrvShoot, SuperSpinDriver, Shoot, Shoot);
    NEW_NERVE(SuperSpinDriverNrvCoolDown, SuperSpinDriver, CoolDown);
};  // namespace NrvSuperSpinDriver

SuperSpinDriver::SuperSpinDriver(const char* pName, s32 color)
    : LiveActor(pName), mBindActor(), mShootPath(), mSpinDriverCamera(), mOperateRing(), mPathDrawer(), mEmptyModel(), mInitBindQuat(0, 0, 0, 1),
      mBindQuat(0, 0, 0, 1), mBindPos(0, 0, 0), mBindVelocity(0, 0, 0), mShootPathDirection(0, 1, 0), mFront(0, 0, 1), mSide(1, 0, 0), mUp(0, 1, 0),
      mShootPathPosition(0, 0, 0), mCaptureStartPos(0, 0, 0), mClippingCenter(0, 0, 0), mClippingRadius(), mSwingAngle(), mSwingAngleMax(),
      mFrontAngle(), mSpinSpeed(), mRollAngle(), mShadowLength(-1.0f), mFlightTime(300), mFlyLoopStep(50), mFlyEndStep(230), mFallStep(280),
      mSpaceFlyStartFrame(::sDefaultSpaceFlyStartFrame), mSpaceFlyEndFrame(::sDefaultSpaceFlyEndFrame), mDrawPathRangeIdx(-1),
      mPlayerLandRotation(), mAlreadyDoneFlag(), mAlreadyDoneFlagIdx(-1), mColor(color), mIsPlayerAway(true), mCoolDown(), mIsCoolingDown(),
      mIsPullPlayer(true), mIsDisableJingle() {
    lumacore::setClass(this, lumacore::kClassId< SuperSpinDriver >);
}

void SuperSpinDriver::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("SuperSpinDriver", nullptr, false);
    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);

    initParamFromPlacement(rIter);
    initGravityAxis();
    initSensor();
    initEffectKeeper(0, nullptr);
    initSound(6, false);
    initEventCamera(rIter);
    initNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvWait));

    MR::initShadowVolumeFlatModel(this, "SuperSpinDriverShadow", MR::getJointMtx(this, "Outside"));
    initOperateRing();
    initShootPath(rIter);
    initShootPathDraw(rIter);
    MR::setClippingFar200m(this);
    initColor();
    initEmptyModel();

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &SuperSpinDriver::requestActive));

        if (isRightToUse()) {
            setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvNonActive));
        } else {
            setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvEmptyNonActive));
        }
    }

    if (MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOnOffB(this, MR::Functor(this, &SuperSpinDriver::requestShow), MR::Functor(this, &SuperSpinDriver::requestHide));
    }

    MR::useStageSwitchSleep(this, rIter);
    initAppearState(rIter);
}

void SuperSpinDriver::initAfterPlacement() {
    SpinDriverUtil::setShadowAndClipping(this, &mClippingCenter, mShadowLength, ::sClippingRadius, &mClippingRadius);

    if (mEmptyModel != nullptr) {
        SpinDriverUtil::setShadowAndClipping(mEmptyModel, &mClippingCenter, mShadowLength, ::sClippingRadius, &mClippingRadius);
    }
}

void SuperSpinDriver::initParamFromPlacement(const TableIter& rIter) {
    MR::getPlacementArg0NoInit(rIter, &mFlightTime);

    mShadowLength = -1.0f;
    MR::getPlacementArg1NoInit(rIter, &mShadowLength);

    s32 isPullPlayer = 0;

    if (MR::getPlacementArg2NoInit(rIter, &isPullPlayer)) {
        mIsPullPlayer = isPullPlayer != 0;
    }

    s32 isDisableJingle = 0;

    if (MR::getPlacementArg4NoInit(rIter, &isDisableJingle)) {
        mIsDisableJingle = isDisableJingle == 1;
    }
}

void SuperSpinDriver::initGravityAxis() {
    MR::offCalcGravity(this);

    Vec3f gravity;
    MR::calcGravityVectorOrZero(this, &gravity, nullptr, 0);

    if (MR::isNearZero(gravity)) {
        gravity.set< f32 >(0.0f, -1.0f, 0.0f);
    }

    mGravity.set(gravity);
}

void SuperSpinDriver::initSensor() {
    initHitSensor(2);
    MR::addHitSensor(this, "bind", ATYPE_SPIN_CANNON_BIND, 16, ::sSensorRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorPosRide(this, "body", 16, ::sSensorRadius, &mBindPos, Vec3f(0.0f, 0.0f, 0.0f));
    MR::invalidateHitSensor(this, "body");
}

void SuperSpinDriver::initEmptyModel() {
    if (!isNeedEmptyModel()) {
        return;
    }

    mEmptyModel = MR::createModelObjMapObjStrongLight("無効モデル", "SuperSpinDriverEmpty", getBaseMtx());
    MR::startBtp(mEmptyModel, "SuperSpinDriverEmpty");
    MR::setBtpFrameAndStop(mEmptyModel, mColor);
    MR::initShadowVolumeFlatModel(mEmptyModel, "SuperSpinDriverShadow", MR::getJointMtx(this, "Outside"));
    mEmptyModel->mGravity.set(mGravity);

    if (isRightToUse()) {
        onUse();
    } else {
        offUse();
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvEmptyWait));
    }
}

void SuperSpinDriver::initEventCamera(const TableIter& rIter) {
    mSpinDriverCamera = new SpinDriverCamera();
    mSpinDriverCamera->init(rIter, this);
}

void SuperSpinDriver::initShootPath(const TableIter& rIter) {
    mPlayerLandRotation = 0.0f;
    MR::getPlacementArg5NoInit(rIter, &mPlayerLandRotation);

    mShootPath = new SpinDriverShootPath();
    mShootPath->initUsingParabolic(rIter, mPosition);
    mShootPath->calcInitPose(&mUp, &mFront, &mSide, mPlayerLandRotation);
}

void SuperSpinDriver::initShootPathDraw(const TableIter& rIter) {
    s32 drawPathRangeIdx = -1;
    f32 coord = 0.0f;

    MR::getPlacementArg7NoInit(rIter, &drawPathRangeIdx);
    mDrawPathRangeIdx = MR::registerStorageSpinDriverPathDrawRange(this, rIter, drawPathRangeIdx, &coord);

    mPathDrawer = new SpinDriverPathDrawer(mShootPath);
    mPathDrawer->setCoord(coord);
    mPathDrawer->initWithoutIter();
}

void SuperSpinDriver::initColor() {
    MR::startBtp(this, "SuperSpinDriver");
    MR::setBtpFrameAndStop(this, mColor);

    switch (mColor) {
    case SuperSpinDriverColor_Yellow:
        MR::startBrk(this, "Yellow");
        mPathDrawer->setColorNormal();
        break;
    case SuperSpinDriverColor_Green:
        MR::startBrk(this, "Green");
        mPathDrawer->setColorGreen();
        break;
    case SuperSpinDriverColor_Pink:
        MR::startBrk(this, "Pink");
        mPathDrawer->setColorPink();
        break;
    }
}

void SuperSpinDriver::initOperateRing() {
    mOperateRing = new SpinDriverOperateRing("操作リング");
    mOperateRing->initWithoutIter();
}

void SuperSpinDriver::initAppearState(const TableIter& rIter) {
    bool isAppear = true;

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        mSpinDriverCamera->initAppearCamera(rIter, this);
        isAppear = false;
    }

    if (MR::tryRegisterDemoCast(this, rIter) && MR::isRegisteredDemoActionAppear(this)) {
        isAppear = false;
    }

    if (MR::joinToGroupArray(this, rIter, "惑星出現", 32)) {
        mSpinDriverCamera->initAppearCamera(rIter, this);
        isAppear = false;
    }

    if (isAppear) {
        makeActorAppeared();
        return;
    }

    // Respawn type 1 remembers the appearance across visits.
    s32 respawnType = 0;
    MR::getPlacementArg6NoInit(rIter, &respawnType);

    mAlreadyDoneFlag = 0;
    mAlreadyDoneFlagIdx = -1;

    if (respawnType == 1) {
        mAlreadyDoneFlagIdx = MR::setupAlreadyDoneFlag("ドライバ出現済み", rIter, &mAlreadyDoneFlag);
    }

    if (mAlreadyDoneFlag != 0) {
        makeActorAppeared();
    } else {
        makeActorDead();
    }
}

void SuperSpinDriver::makeActorAppeared() {
    LiveActor::makeActorAppeared();

    if (mPathDrawer != nullptr && mPathDrawer->mMaxCoord > 0.0f) {
        startPathDraw();
    }
}

void SuperSpinDriver::appear() {
    if (!MR::isDead(this)) {
        return;
    }

    LiveActor::appear();

    if (isRightToUse()) {
        onUse();
        requestAppear();
    } else {
        offUse();
        requestEmptyAppear();
    }

    if (mAlreadyDoneFlagIdx >= 0) {
        MR::updateAlreadyDoneFlag(mAlreadyDoneFlagIdx, 1);
    }
}

void SuperSpinDriver::control() {
    if (!mIsPlayerAway && isFarPlayer()) {
        mIsPlayerAway = true;
    }

    if (mCoolDown > 0) {
        mCoolDown--;
    }

    mIsCoolingDown = mCoolDown > 0;

    mFrontAngle = MR::repeat(mFrontAngle + mSpinSpeed, -PI, TWO_PI);
    mSpinSpeed *= ::sSpinDamp;
}

void SuperSpinDriver::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::makeMtxUpFrontPos(&mtx, mUp, mFront, mPosition);

    Pos3f spinMtx;
    spinMtx.identity();
    spinMtx.setEulerY(mFrontAngle);

    mtx.concat(mtx, spinMtx);
    MR::setBaseTRMtx(this, mtx);
}

void SuperSpinDriver::startClipped() {
    LiveActor::startClipped();
    MR::deleteEffectAll(this);
}

void SuperSpinDriver::endClipped() {
    LiveActor::endClipped();
    endPathDraw();
}

void SuperSpinDriver::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mBindActor != nullptr && !MR::isSensorAutoRush(pSender)) {
        MR::tryGetItem(pSender, pReceiver);
    }
}

bool SuperSpinDriver::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    switch (msg) {
    case ACTMES_IS_RUSH_TAKEOVER:
        mCoolDown = ::sCoolDownFrame;
        return canBind(pSender, pReceiver);
    case ACTMES_AUTORUSH_BEGIN:
        mCoolDown = ::sCoolDownFrame;
        return tryBind(pSender, pReceiver);
    case ACTMES_RUSH_CANCEL:
        if (isNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvShootStart)) ||
            (isNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvShoot)) && MR::isLessStep(this, ::sShootNoCancelStep))) {
            return false;
        }

        return tryReleaseBind();
    case ACTMES_RUSH_FORCE_CANCEL:
        return tryReleaseBind();
    case ACTMES_UPDATE_BASEMTX:
        if (mBindActor == nullptr) {
            return false;
        }

        updateBindActorMatrix();
        return true;
    }

    return false;
}

bool SuperSpinDriver::tryReleaseBind() {
    if (mBindActor == nullptr) {
        return false;
    }

    mBindActor = nullptr;
    mSpinDriverCamera->cancel();
    return true;
}

// A player who walked away is pulled in when back in range.
bool SuperSpinDriver::isPullPlayer(HitSensor* pSender, HitSensor* pReceiver) const {
    return mIsPullPlayer && mIsPlayerAway && MR::isNear(pSender, pReceiver, ::sPullPlayerDistance);
}

bool SuperSpinDriver::canBind(HitSensor* pSender, HitSensor* pReceiver) const {
    return isNerveEnableBind() && (isSwingOr2PTrigger() || isPullPlayer(pSender, pReceiver));
}

bool SuperSpinDriver::tryBind(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerveEnableBind()) {
        return false;
    }

    if (isSwingOr2PTrigger()) {
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvShootStart));
    } else if (isPullPlayer(pSender, pReceiver)) {
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvCapture));
    } else {
        return false;
    }

    mBindActor = pSender->mHost;
    mBindPos = mBindActor->mPosition;
    mBindVelocity = *MR::getPlayerLastMove();

    f32 speed = mBindVelocity.length();

    if (speed > ::sBindSpeedMax) {
        mBindVelocity *= ::sBindSpeedMax / speed;
    }

    mSwingAngle = 0.0f;
    mRollAngle = 0.0f;
    Pos3f bindActorBaseMtx(mBindActor->getBaseMtx());
    bindActorBaseMtx.getQuat(mInitBindQuat);
    mBindQuat = mInitBindQuat;

    mSpaceFlyStartFrame = MR::getBckFrameMax(mBindActor, "SpaceFlyStart");
    mSpaceFlyEndFrame = MR::getBckFrameMax(mBindActor, "SpaceFlyEnd");

    MR::invalidateClipping(this);

    mSpinDriverCamera->start(mUp, mFront, mPosition);
    return true;
}

bool SuperSpinDriver::tryEndCapture() {
    if (MR::isGreaterStep(this, ::sCaptureFrame) && mBindPos.distance(mPosition) < ::sCaptureEndDistance) {
        cancelBind();
        mIsPlayerAway = false;
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvWait));
        return true;
    }

    return false;
}

bool SuperSpinDriver::tryForceCancel() {
    if (mBindActor == nullptr) {
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvCoolDown));
        return true;
    }

    return false;
}

bool SuperSpinDriver::tryShootStart() {
    if (isSwingOr2PTrigger()) {
        MR::deleteEffect(this, ::cLightEffect);
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvShootStart));
        return true;
    }

    return false;
}

bool SuperSpinDriver::tryShoot() {
    if (MR::isGreaterStep(this, ::sShootWaitFrame)) {
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvShoot));
        return true;
    }

    return false;
}

bool SuperSpinDriver::tryEndShoot() {
    if (MR::isGreaterEqualStep(this, mFlightTime)) {
        endBind();
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvCoolDown));
        return true;
    }

    return false;
}

bool SuperSpinDriver::tryEndCoolDown() {
    if (MR::isGreaterStep(this, ::sCoolDownWaitStep) && mCoolDown == 0) {
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvWait));
        return true;
    }

    return false;
}

bool SuperSpinDriver::trySwitchOff() {
    if (MR::isValidSwitchAppear(this) && !MR::isOnSwitchAppear(this)) {
        kill();
        return true;
    }

    return false;
}

void SuperSpinDriver::requestAppearNerve(const Nerve* pAppearNerve) {
    MR::invalidateClipping(this);

    if (mSpinDriverCamera->isUseAppearCamera(this)) {
        MR::requestStartDemo(this, ::cDemoNameAppear, pAppearNerve, GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvTryDemo));
    } else {
        setNerve(pAppearNerve);
    }
}

void SuperSpinDriver::requestAppear() {
    requestAppearNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvAppear));
}

void SuperSpinDriver::requestEmptyAppear() {
    requestAppearNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvEmptyAppear));
}

void SuperSpinDriver::requestActive() {
    if (isNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvNonActive))) {
        requestAppear();
    } else if (isNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvEmptyNonActive))) {
        requestEmptyAppear();
    }
}

void SuperSpinDriver::requestHide() {
    if (MR::isDead(this)) {
        return;
    }

    if (mBindActor != nullptr) {
        endBind();
    }

    makeActorDead();
}

void SuperSpinDriver::requestShow() {
    if (MR::isDead(this)) {
        makeActorAppeared();
    }
}

void SuperSpinDriver::startAppearCamera() {
    mSpinDriverCamera->startAppearCamera(this, mUp, mFront, mPosition);

    if (!mIsDisableJingle) {
        MR::startSystemSE("SE_SY_SPIN_DRIVER_APPEAR");
        MR::startSound(this, "SE_OJ_S_SPIN_DRV_APPEAR");
    }
}

// After the appear animation and the camera's frames.
void SuperSpinDriver::tryEndAppear(const Nerve* pNextNerve) {
    if (!MR::isBckStopped(this) || !MR::isGreaterStep(this, mSpinDriverCamera->getAppearCameraFrames())) {
        return;
    }

    setNerve(pNextNerve);

    if (mSpinDriverCamera->isUseAppearCamera(this)) {
        mSpinDriverCamera->endAppearCamera(this);
        MR::endDemoWaitCameraInterpolating(this, ::cDemoNameAppear);
    }
}

// Waits in the demo request until the demo starts the appear nerve.
void SuperSpinDriver::exeTryDemo() {
}

void SuperSpinDriver::exeEmptyNonActive() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
    }

    if (isRightToUse()) {
        onUse();
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvNonActive));
    }
}

void SuperSpinDriver::exeEmptyAppear() {
    if (MR::isFirstStep(this)) {
        startAppearCamera();
    }

    tryEndAppear(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvEmptyWait));
}

void SuperSpinDriver::exeEmptyWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
    }

    if (isRightToUse()) {
        onUse();
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvWait));
    }
}

void SuperSpinDriver::exeNonActive() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "NonActive");
        MR::validateClipping(this);
    }

    addSwingSignRotateY();
}

void SuperSpinDriver::exeAppear() {
    if (MR::isFirstStep(this)) {
        startAppearCamera();
        MR::startBck(this, "Appear");
        mSpinSpeed = 0.0f;
    }

    tryEndAppear(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvWait));
}

void SuperSpinDriver::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::validateClipping(this);
    }

    if (MR::isGreaterStep(this, ::sCanBindTime)) {
        addSwingSignRotateY();
    }

    // A player just bound or rushed in: shine, and light up on the first frame of it.
    if (mCoolDown > 0) {
        MR::startLevelSound(this, "SE_OJ_LV_S_SPIN_DRV_SHINE");

        if (!mIsCoolingDown) {
            MR::emitEffect(this, ::cLightEffect);
            MR::startCSSound("CS_SPIN_BIND", nullptr, 0);
        }
    } else {
        MR::deleteEffect(this, ::cLightEffect);
    }

    trySwitchOff();
}

void SuperSpinDriver::exeCapture() {
    if (tryForceCancel()) {
        MR::deleteEffect(this, ::cLightEffect);
        return;
    }

    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, ::cLightEffect);
        MR::startBckPlayer("SpinDriverWait", "SuperSpinDriverCapture");
        mSpinSpeed = 0.0f;
    }

    MR::startLevelSound(this, "SE_OJ_LV_S_SPIN_DRV_SHINE");
    MR::startLevelSound(this, "SE_OJ_LV_SPIN_DRV_CAPTURE");
    moveBindPosToCenter();
    updateBindActorPoseToShoot(MR::calcNerveRate(this, ::sCaptureFrame));
    mSpinSpeed += ::sCaptureSpinAccel;
    MR::tryRumblePadWeak(this, PLAYER_1);
    mCoolDown = ::sCoolDownFrame;

    if (!tryEndCapture()) {
        tryShootStart();
    }
}

void SuperSpinDriver::endShoot() {
    MR::invalidateHitSensor(this, "body");
    mOperateRing->reset();
}

void SuperSpinDriver::exeCoolDown() {
    if (!tryEndCoolDown()) {
        trySwitchOff();
    }
}

void SuperSpinDriver::exeShootStart() {
    if (tryForceCancel()) {
        return;
    }

    if (MR::isFirstStep(this)) {
        MR::startSound(mBindActor, "SE_PM_SPIN_ATTACK");
        MR::startCSSound("CS_SPIN_DRIVE_LONG", "SE_SY_CS_S_SPIN_DRV_START", 0);
        MR::startSound(this, "SE_OJ_S_SPIN_DRV_PREP_JUMP");

        if (MR::isInAreaObj("Water", mPosition)) {
            MR::startSound(this, "SE_PM_SPIN_DRV_IN_WATER_1");
        }

        MR::deleteEffectAll(this);
        MR::emitEffect(this, "SuperSpinDriverStart");
        MR::startBck(this, "Start");
        MR::startBckPlayer("SuperSpinDriverStart", "SuperSpinDriverShoot");
        mCaptureStartPos = mBindPos;
        updateBindActorPoseToShoot(1.0f);
    }

    f32 rate = MR::calcNerveRate(this, ::sShootStartFrame);

    mBindPos = mPosition * rate + mCaptureStartPos * (1.0f - rate);
    mSpinSpeed *= ::sShootStartSpinDamp;

    MR::tryRumblePadMiddle(this, PLAYER_1);
    tryShoot();
}

void SuperSpinDriver::exeShoot() {
    if (tryForceCancel()) {
        return;
    }

    if (MR::isFirstStep(this)) {
        calcShootMotionTime();
        MR::validateHitSensor(this, "body");

        if (MR::hasME()) {
            MR::startSystemME("ME_MAGIC_L");
        } else {
            MR::startSystemSE("SE_SY_S_SPIN_DRV_ME_ALT");
        }

        MR::startSound(this, "SE_OJ_S_SPIN_DRV_JUMP");
        MR::startSound(mBindActor, "SE_PV_JUMP_JOY");

        if (MR::isInAreaObj("Water", mPosition)) {
            MR::startSound(this, "SE_PM_SPIN_DIV_IN_WATER_2");
        }

        MR::startBckPlayer("SpaceFlyStart", "SuperSpinDriverFlyStart");
        MR::shakeCameraStrong();

        MR::tryRumblePadVeryStrong(this, PLAYER_1);
        mShootPathDirection.set(mUp);
        mOperateRing->reset();
        startPathDraw();
    }

    f32 rate = MR::calcNerveRate(this, mFlightTime);
    updatePathDraw(rate);
    updateOperateRate();
    updateBindPosition(rate);

    if (!MR::isNearZero(mShootPathDirection)) {
        Vec3f headUp;
        Vec3f steeredDir = mShootPathDirection + (mOperateRing->mDirection * mOperateRing->mRadiusRate) * ::sOperateDirWeight;
        MR::normalize(steeredDir, &headUp);
        turnBindHead(headUp, ::sHeadTurnRate);
    }

    if (mFlyLoopStep <= getNerveStep() && getNerveStep() <= mFlyEndStep) {
        f32 loopRate = MR::normalize(getNerveStep(), mFlyLoopStep, mFlyEndStep);
        mSwingAngle = mSwingAngleMax * MR::getEaseOutValue(loopRate, 0.0f, 1.0f, 1.0f);
    }

    mRollAngle = PI * MR::normalize(getNerveStep(), mFlyEndStep, mFallStep);
    updateShootMotion();
    mSpinDriverCamera->update(mShootPathDirection, mShootPathPosition);
    tryEndShoot();
}

void SuperSpinDriver::updateShootMotion() {
    if (MR::isStep(this, mFlyLoopStep)) {
        MR::startBckPlayer("SpaceFlyLoop", "SuperSpinDriverFlyLoop");
    }

    if (MR::isLessStep(this, mFlyEndStep)) {
        MR::startLevelSound(mBindActor, "SE_PM_LV_S_SPIN_DRV_FLY");
    }

    if (MR::isStep(this, mFlyEndStep)) {
        MR::startBckPlayer("SpaceFlyEnd", "SuperSpinDriverFlyEnd");
        MR::startSound(mBindActor, "SE_PM_S_SPIN_DRV_COOL_DOWN");
        MR::startSound(mBindActor, "SE_PV_JUMP_S");
    }

    if (MR::isStep(this, mFallStep)) {
        MR::startBckPlayer("Fall", "SuperSpinDriverFall");
    }
}

void SuperSpinDriver::cancelBind() {
    if (mBindActor != nullptr) {
        MR::endBindAndPlayerJump(this, mBindVelocity, 0);
        mBindActor = nullptr;
    }

    mSpinDriverCamera->cancel();
}

void SuperSpinDriver::endBind() {
    MR::endBindAndSpinDriverJump(this, mBindVelocity);
    mBindActor = nullptr;
    mSpinDriverCamera->end();
}

// The bound player: swing about its up axis, roll over at the end, dropped while steering.
void SuperSpinDriver::updateBindActorMatrix() {
    Pos3f swingMtx;
    swingMtx.identity();
    swingMtx.setEulerY(mSwingAngle);
    f32 offset = ::sOperateDropOffset;
    offset *= mOperateRing->mRadiusRate;
    swingMtx.setTrans(0.0f, offset, 0.0f);

    Pos3f rollMtx;
    rollMtx.identity();
    rollMtx.setEulerX(mRollAngle);

    Pos3f mtx;
    mtx.setQT(mBindQuat, mBindPos);

    mtx.concat(mtx, rollMtx);
    mtx.concat(mtx, swingMtx);
    MR::setBaseTRMtx(mBindActor, mtx);
}

void SuperSpinDriver::updateBindActorPoseToShoot(f32 rate) {
    Pos3f shootMtx;
    shootMtx.identity();

    MR::makeMtxUpFront(&shootMtx, mUp, mFront);

    Quat4f shootQuat;
    shootMtx.getQuat(shootQuat);

    mBindQuat.set(mInitBindQuat);
    mBindQuat.slerp(shootQuat, rate);
}

void SuperSpinDriver::turnBindHead(const Vec3f& rUp, f32 rate) {
    Vec3f bindUp;
    mBindQuat.getYDir(bindUp);

    Quat4f turn;
    turn.setRotate(bindUp, rUp, rate);

    mBindQuat.mult(turn);
    mBindQuat.normalize();
}

void SuperSpinDriver::moveBindPosToCenter() {
    mBindPos += mBindVelocity;

    Vec3f toCenter(mPosition - mBindPos);
    f32 dist;
    MR::separateScalarAndDirection(&dist, &toCenter, toCenter);

    mBindVelocity += toCenter * ::sCapturePullRate * (dist / ::sCapturePullDistance);
    mBindVelocity.mult(::sCaptureDamp);
}

void SuperSpinDriver::startPathDraw() {
    if (mPathDrawer != nullptr && MR::isDead(mPathDrawer)) {
        mPathDrawer->appear();
        MR::emitEffect(this, ::cGlowEffect);
    }
}

void SuperSpinDriver::endPathDraw() {
    if (mPathDrawer != nullptr && !MR::isDead(mPathDrawer)) {
        MR::emitEffect(this, ::cGlowEffect);
    }
}

void SuperSpinDriver::updatePathDraw(f32 coord) {
    if (mPathDrawer == nullptr) {
        return;
    }

    mPathDrawer->setCoord(coord);

    if (mDrawPathRangeIdx >= 0) {
        MR::updateStorageSpinDriverPathDrawRange(mDrawPathRangeIdx, mPathDrawer->mMaxCoord);
    }
}

// The reference ramps the steering ring only behind a flag nothing sets; the ring stays off.
void SuperSpinDriver::updateOperateRate() {
    mOperateRing->setRadiusRate(0.0f);
}

void SuperSpinDriver::updateBindPosition(f32 coord) {
    mShootPath->calcPosition(&mShootPathPosition, coord);

    Vec3f direction;
    mShootPath->calcDirection(&direction, coord, ::sPathDirectionDelta);

    if (!MR::isNearZero(direction)) {
        mShootPathDirection = direction;
    }

    mOperateRing->update(mShootPathPosition, mShootPathDirection);
    Vec3f lastBindPos = mBindPos;
    mBindPos.set(mShootPathPosition + mOperateRing->mOffset);
    mBindVelocity = mBindPos - lastBindPos;
}

// Fly start, a loop from a whole number of start lengths in, the fly end, then sFallFrame of fall.
void SuperSpinDriver::calcShootMotionTime() {
    if (mFlightTime < ::sFallFrame) {
        mFlyLoopStep = -1;
        mFlyEndStep = -1;
        mFallStep = 0;
        return;
    }

    if (mFlightTime < ::sFallFrame + mSpaceFlyEndFrame) {
        mFlyLoopStep = -1;
        mFlyEndStep = 0;
        mFallStep = mFlightTime - ::sFallFrame;
        return;
    }

    mFlyEndStep = mFlightTime - (::sFallFrame + mSpaceFlyEndFrame);
    mFallStep = mFlightTime - ::sFallFrame;

    s32 loopStart = ::sFlyLoopStartRate * mFlightTime;

    if (loopStart > ::sFlyLoopStartMax) {
        loopStart = ::sFlyLoopStartMax;
    }

    mFlyLoopStep = mSpaceFlyStartFrame != 0 ? (loopStart / mSpaceFlyStartFrame) * mSpaceFlyStartFrame : 0;

    if (mFlyEndStep <= mFlyLoopStep) {
        mFlyLoopStep = 0;
    }

    s32 swingTurns = ((mFlyEndStep - mFlyLoopStep) * ::sSwingTurnPerStep) / TWO_PI;
    mSwingAngle = 0.0f;
    mSwingAngleMax = TWO_PI * swingTurns;
}

void SuperSpinDriver::addSwingSignRotateY() {
    if (isSwingOr2PTrigger()) {
        mSpinSpeed += ::sSwingSpinAccel;

        if (mSpinSpeed > ::sSwingSpinMax) {
            mSpinSpeed = ::sSwingSpinMax;
        }
    }
}

void SuperSpinDriver::onUse() {
    if (mEmptyModel != nullptr) {
        mEmptyModel->kill();
    }

    MR::showModel(this);
}

void SuperSpinDriver::offUse() {
    if (mEmptyModel != nullptr) {
        mEmptyModel->appear();
    }

    MR::hideModelAndOnCalcAnim(this);
}

bool SuperSpinDriver::isNeedEmptyModel() const {
    return mColor == SuperSpinDriverColor_Green;
}

bool SuperSpinDriver::isRightToUse() const {
    return mColor != SuperSpinDriverColor_Green || MR::isOnGameEventFlagGreenDriver();
}

bool SuperSpinDriver::isNerveEnableBind() const {
    return isNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvWait)) && MR::isGreaterStep(this, ::sCanBindTime);
}

bool SuperSpinDriver::isSwingOr2PTrigger() const {
    return MR::isSpin(PLAYER_1) || MR::isPlayerPointedBy2POnTriggerButton();
}

bool SuperSpinDriver::isFarPlayer() const {
    return !MR::isNearPlayerAnyTime(this, ::sFarPlayerDistance);
}

namespace MR {
    NameObj* createSuperSpinDriverYellow(const char* pName) {
        return new SuperSpinDriver(pName, SuperSpinDriverColor_Yellow);
    }

    NameObj* createSuperSpinDriverGreen(const char* pName) {
        return new SuperSpinDriver(pName, SuperSpinDriverColor_Green);
    }

    NameObj* createSuperSpinDriverPink(const char* pName) {
        return new SuperSpinDriver(pName, SuperSpinDriverColor_Pink);
    }
};  // namespace MR
