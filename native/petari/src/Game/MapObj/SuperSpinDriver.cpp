#include "Game/MapObj/SuperSpinDriver.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/SpinDriverCamera.hpp"
#include "Game/MapObj/SpinDriverOperateRing.hpp"
#include "Game/MapObj/SpinDriverPathDrawer.hpp"
#include "Game/MapObj/SpinDriverShootPath.hpp"
#include "Game/MapObj/SpinDriverUtil.hpp"
#include "Game/Util.hpp"

namespace {
    static const f32 sCanBindTime = 90;
    static s32 cSpaceFlyStartFrame = 10;
    static s32 cSpaceFlyLoopFrame = 100;
    static s32 cSpaceFlyEndFrame = 50;
};  // namespace

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
      mShootPathPosition(0, 0, 0), mCaptureStartPos(0, 0, 0), mClippingCenter(0, 0, 0), mSwingAngle(), mSwingAngleMax(), mFrontAngle(), mSpinSpeed(), mRollAngle(), mShadowLength(-1.0f),
      mFlightTime(300), mFlyLoopStep(50), mFlyEndStep(230), mFallStep(280), mDrawPathRangeIdx(-1), mPlayerLandRotation(), mAlreadyDoneFlag(), mAlreadyDoneFlagIdx(-1), mColor(color),
      mIsPlayerAway(true), mCoolDown(), mIsCoolingDown(), mIsPullPlayer(true), mIsDisableJingle() {
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
    SpinDriverUtil::setShadowAndClipping(this, &mClippingCenter, mShadowLength, 300.0f, &mClippingRadius);

    if (mEmptyModel != nullptr) {
        SpinDriverUtil::setShadowAndClipping(mEmptyModel, &mClippingCenter, mShadowLength, 300.0f, &mClippingRadius);
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
    MR::addHitSensor(this, "bind", ATYPE_SPIN_CANNON_BIND, 16, 300.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorPosRide(this, "body", 16, 300.0f, &mBindPos, Vec3f(0.0f, 0.0f, 0.0f));
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
    case 0:
        MR::startBrk(this, "Yellow");
        mPathDrawer->setColorNormal();
        break;
    case 1:
        MR::startBrk(this, "Green");
        mPathDrawer->setColorGreen();
        break;
    case 2:
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

    s32 respawnType = 0;
    MR::getPlacementArg6NoInit(rIter, &respawnType);

    if (respawnType == 1) {
        mAlreadyDoneFlag = 0;
        mAlreadyDoneFlagIdx = MR::setupAlreadyDoneFlag("ドライバ出現済み", rIter, &mAlreadyDoneFlag);
    } else {
        mAlreadyDoneFlag = 0;
        mAlreadyDoneFlagIdx = -1;
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

    mFrontAngle += mSpinSpeed;
    mFrontAngle = MR::repeat(mFrontAngle, -PI, TWO_PI);
    mSpinSpeed *= 0.985f;
}

void SuperSpinDriver::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::makeMtxUpFrontPos(&mtx, mUp, mFront, mPosition);

    Pos3f mtxRotate;
    mtxRotate.identity();
    mtxRotate.setEulerY(mFrontAngle);

    mtx.concat(mtx, mtxRotate);
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
    if (mBindActor != nullptr && !MR::isSensorAutoRush(pSender) && MR::tryGetItem(pSender, pReceiver)) {
        return;
    }
}

bool SuperSpinDriver::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg == ACTMES_IS_RUSH_TAKEOVER) {
        mCoolDown = 60;
        return canBind(pSender, pReceiver);
    }

    if (msg == ACTMES_AUTORUSH_BEGIN) {
        mCoolDown = 60;

        if (tryBind(pSender, pReceiver)) {
            return true;
        }
    }

    if (msg == ACTMES_RUSH_CANCEL) {
        if (isNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvShootStart)) ||
            (isNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvShoot)) && MR::isLessStep(this, 40))) {
            return false;
        }

        if (mBindActor != nullptr) {
            mBindActor = nullptr;
            mSpinDriverCamera->cancel();
            return true;
        }
    }

    if (msg == ACTMES_RUSH_FORCE_CANCEL && mBindActor != nullptr) {
        mBindActor = nullptr;
        mSpinDriverCamera->cancel();
        return true;
    }

    if (msg == ACTMES_UPDATE_BASEMTX && mBindActor != nullptr) {
        updateBindActorMatrix();
        return true;
    }

    return false;
}

bool SuperSpinDriver::canBind(HitSensor* pSender, HitSensor* pReceiver) const {
    if (!isNerveEnableBind()) {
        return false;
    }

    if (isSwingOr2PTrigger()) {
        return true;
    }

    if (mIsPullPlayer && mIsPlayerAway && MR::isNear(pSender, pReceiver, 240.0f)) {
        return true;
    }

    return false;
}

bool SuperSpinDriver::tryBind(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerveEnableBind()) {
        return false;
    }

    if (isSwingOr2PTrigger()) {
        mBindActor = pSender->mHost;
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvShootStart));
    } else if (mIsPullPlayer && mIsPlayerAway && MR::isNear(pSender, pReceiver, 240.0f)) {
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvCapture));
    } else {
        return false;
    }

    mBindActor = pSender->mHost;
    mBindPos = mBindActor->mPosition;
    mBindVelocity = *MR::getPlayerLastMove();

    f32 length = mBindVelocity.length();

    if (length > 40.0f) {
        mBindVelocity *= 40.0f / length;
    }

    mSwingAngle = 0.0f;
    mRollAngle = 0.0f;
    Pos3f bindActorBaseMtx(mBindActor->getBaseMtx());
    bindActorBaseMtx.getQuat(mInitBindQuat);
    mBindQuat = mInitBindQuat;

    ::cSpaceFlyStartFrame = MR::getBckFrameMax(mBindActor, "SpaceFlyStart");
    ::cSpaceFlyLoopFrame = MR::getBckFrameMax(mBindActor, "SpaceFlyLoop");
    ::cSpaceFlyEndFrame = MR::getBckFrameMax(mBindActor, "SpaceFlyEnd");

    MR::invalidateClipping(this);

    mSpinDriverCamera->start(mUp, mFront, mPosition);
    return true;
}

bool SuperSpinDriver::tryEndCapture() {
    if (MR::isGreaterStep(this, 60) && mBindPos.distance(mPosition) < 15.0f) {
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
    bool isSwingOrPointed = MR::isSpin(PLAYER_1) || MR::isPlayerPointedBy2POnTriggerButton();

    if (isSwingOrPointed) {
        MR::deleteEffect(this, "SuperSpinDriverLight");
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvShootStart));
        return true;
    }

    return false;
}

bool SuperSpinDriver::tryShoot() {
    if (MR::isGreaterStep(this, 45)) {
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
    if (MR::isGreaterStep(this, 60) && mCoolDown == 0) {
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

void SuperSpinDriver::requestAppear() {
    MR::invalidateClipping(this);

    if (mSpinDriverCamera->isUseAppearCamera(this)) {
        MR::requestStartDemo(this, "出現", GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvAppear),
                             GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvTryDemo));
    } else {
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvAppear));
    }
}

void SuperSpinDriver::requestEmptyAppear() {
    MR::invalidateClipping(this);

    if (mSpinDriverCamera->isUseAppearCamera(this)) {
        MR::requestStartDemo(this, "出現", GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvEmptyAppear),
                             GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvTryDemo));
    } else {
        setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvEmptyAppear));
    }
}

void SuperSpinDriver::requestActive() {
    if (isNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvNonActive))) {
        requestAppear();
    } else if (isNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvEmptyNonActive))) {
        requestEmptyAppear();
    }
}

void SuperSpinDriver::requestHide() {
    if (!MR::isDead(this)) {
        if (mBindActor != nullptr) {
            endBind();
        }

        makeActorDead();
    }
}

void SuperSpinDriver::requestShow() {
    if (MR::isDead(this)) {
        makeActorAppeared();
    }
}

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
        mSpinDriverCamera->startAppearCamera(this, mUp, mFront, mPosition);

        if (!mIsDisableJingle) {
            MR::startSystemSE("SE_SY_SPIN_DRIVER_APPEAR");
            MR::startSound(this, "SE_OJ_S_SPIN_DRV_APPEAR");
        }
    }

    if (MR::isBckStopped(this)) {
        s32 frames = mSpinDriverCamera->getAppearCameraFrames();

        if (MR::isGreaterStep(this, frames)) {
            setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvEmptyWait));

            if (mSpinDriverCamera->isUseAppearCamera(this)) {
                mSpinDriverCamera->endAppearCamera(this);
                MR::endDemoWaitCameraInterpolating(this, "出現");
            }
        }
    }
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
        mSpinDriverCamera->startAppearCamera(this, mUp, mFront, mPosition);

        if (!mIsDisableJingle) {
            MR::startSystemSE("SE_SY_SPIN_DRIVER_APPEAR");
            MR::startSound(this, "SE_OJ_S_SPIN_DRV_APPEAR");
        }

        MR::startBck(this, "Appear");
        mSpinSpeed = 0.0f;
    }

    if (MR::isBckStopped(this)) {
        s32 frames = mSpinDriverCamera->getAppearCameraFrames();

        if (MR::isGreaterStep(this, frames)) {
            setNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvWait));

            if (mSpinDriverCamera->isUseAppearCamera(this)) {
                mSpinDriverCamera->endAppearCamera(this);
                MR::endDemoWaitCameraInterpolating(this, "出現");
            }
        }
    }
}

void SuperSpinDriver::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
        MR::validateClipping(this);
    }

    if (MR::isGreaterStep(this, ::sCanBindTime)) {
        addSwingSignRotateY();
    }

    if (mCoolDown > 0) {
        MR::startLevelSound(this, "SE_OJ_LV_S_SPIN_DRV_SHINE");

        if (!mIsCoolingDown) {
            MR::emitEffect(this, "SuperSpinDriverLight");
            MR::startCSSound("CS_SPIN_BIND", nullptr, 0);
        }

    } else {
        MR::deleteEffect(this, "SuperSpinDriverLight");
    }

    if (trySwitchOff()) {
        (void)0.0f;
    }
}

void SuperSpinDriver::exeCapture() {
    if (tryForceCancel()) {
        MR::deleteEffect(this, "SuperSpinDriverLight");
    } else {
        if (MR::isFirstStep(this)) {
            MR::emitEffect(this, "SuperSpinDriverLight");
            MR::startBckPlayer("SpinDriverWait", "SuperSpinDriverCapture");
            mSpinSpeed = 0.0f;
        }

        MR::startLevelSound(this, "SE_OJ_LV_S_SPIN_DRV_SHINE");
        MR::startLevelSound(this, "SE_OJ_LV_SPIN_DRV_CAPTURE");
        moveBindPosToCenter();
        f32 rate = MR::calcNerveRate(this, 60);
        updateBindActorPoseToShoot((f64)rate);
        mSpinSpeed += 0.0040f;
        MR::tryRumblePadWeak(this, PLAYER_1);
        mCoolDown = 60;

        if (!tryEndCapture()) {
            if (!tryShootStart()) {
                return;
            }
        }
    }
}

void SuperSpinDriver::endShoot() {
    MR::invalidateHitSensor(this, "body");
    mOperateRing->reset();
}

void SuperSpinDriver::exeCoolDown() {
    // BUG, is supposed to be a conditional to call tryEndCoolDown

    if (MR::isFirstStep(this)) {
    }

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

    f32 rate = MR::calcNerveRate(this, 15);

    mBindPos = mPosition * rate + mCaptureStartPos * (1.0f - rate);
    mSpinSpeed *= 0.8f;

    MR::tryRumblePadMiddle(this, PLAYER_1);

    if (tryShoot()) {
        return;
    }
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
        Vec3f stack_2C;
        Vec3f stack_20 = mShootPathDirection + (mOperateRing->mDirection * mOperateRing->mRadiusRate) * 0.8f;
        MR::normalize(stack_20, &stack_2C);
        turnBindHead(stack_2C, 0.4f);
    }

    if (mFlyLoopStep <= getNerveStep() && getNerveStep() <= mFlyEndStep) {
        f32 v = MR::normalize(getNerveStep(), mFlyLoopStep, mFlyEndStep);
        mSwingAngle = mSwingAngleMax * MR::getEaseOutValue(v, 0.0f, 1.0f, 1.0f);
    }

    mRollAngle = PI * MR::normalize(getNerveStep(), mFlyEndStep, mFallStep);
    updateShootMotion();
    mSpinDriverCamera->update(mShootPathDirection, mShootPathPosition);

    if (tryEndShoot()) {
        return;
    }
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

void SuperSpinDriver::updateBindActorMatrix() {
    Pos3f mtx;
    mtx.identity();
    mtx.setEulerY(mSwingAngle);
    f32 offset = -75.0f;
    const f32 rate = mOperateRing->mRadiusRate;
    offset *= rate;
    mtx.setTrans(0.0f, offset, 0.0f);

    Pos3f mtx2;
    mtx2.identity();
    mtx2.setEulerX(mRollAngle);

    Pos3f mtx3;
    mtx3.setQT(mBindQuat, mBindPos);

    mtx3.concat(mtx3, mtx2);
    mtx3.concat(mtx3, mtx);
    MR::setBaseTRMtx(mBindActor, mtx3);
}

void SuperSpinDriver::updateBindActorPoseToShoot(f32 a1) {
    Pos3f stack_18;
    stack_18.identity();

    MR::makeMtxUpFront(&stack_18, mUp, mFront);

    Quat4f stack_8;
    stack_18.getQuat(stack_8);

    mBindQuat.set(mInitBindQuat);
    mBindQuat.slerp(stack_8, a1);
}

void SuperSpinDriver::turnBindHead(const Vec3f& rVec, f32 a2) {
    Vec3f quatYDir;
    mBindQuat.getYDir(quatYDir);

    Quat4f quat;
    quat.setRotate(quatYDir, rVec, a2);

    mBindQuat.mult(quat);
    mBindQuat.normalize();
}

void SuperSpinDriver::moveBindPosToCenter() {
    Vec3f pos(mPosition);
    mBindPos += mBindVelocity;

    Vec3f vec(pos - mBindPos);
    f32 scalar;
    MR::separateScalarAndDirection(&scalar, &vec, vec);

    mBindVelocity += vec * 1.5f * (scalar / 120.0f);

    mBindVelocity.mult(0.8f);
}

void SuperSpinDriver::startPathDraw() {
    if (mPathDrawer != nullptr) {
        if (MR::isDead(mPathDrawer)) {
            mPathDrawer->appear();
            MR::emitEffect(this, "EndGlow");
        }
    }
}

void SuperSpinDriver::endPathDraw() {
    if (mPathDrawer != nullptr) {
        if (!MR::isDead(mPathDrawer)) {
            MR::emitEffect(this, "EndGlow");
        }
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

void SuperSpinDriver::updateOperateRate() {
    mOperateRing->setRadiusRate(0.0f);
}

void SuperSpinDriver::updateBindPosition(f32 coord) {
    mShootPath->calcPosition(&mShootPathPosition, coord);

    Vec3f direction;
    mShootPath->calcDirection(&direction, coord, 0.0099999998f);

    if (!MR::isNearZero(direction)) {
        mShootPathDirection = direction;
    }

    mOperateRing->update(mShootPathPosition, mShootPathDirection);
    Vec3f stack_20 = mBindPos;
    Vec3f stack_14 = mShootPathPosition + mOperateRing->mOffset;
    mBindPos.set(stack_14);
    mBindVelocity = mBindPos - stack_20;
}

void SuperSpinDriver::calcShootMotionTime() {
    if (mFlightTime < 20) {
        mFlyLoopStep = -1;
        mFlyEndStep = -1;
        mFallStep = 0;
        return;
    }

    if (mFlightTime < 20 + ::cSpaceFlyEndFrame) {
        mFlyLoopStep = -1;
        mFlyEndStep = 0;
        mFallStep = mFlightTime - 20;
        return;
    }

    mFlyEndStep = mFlightTime - (20 + ::cSpaceFlyEndFrame);
    mFallStep = mFlightTime - 20;

    s32 temp = 0.2f * mFlightTime;

    if (temp > 90) {
        temp = 90;
    }

    mFlyLoopStep = ::cSpaceFlyStartFrame != 0 ? (temp / ::cSpaceFlyStartFrame) * ::cSpaceFlyStartFrame : 0;

    if (mFlyEndStep <= mFlyLoopStep) {
        mFlyLoopStep = 0;
    }

    s32 temp2 = ((mFlyEndStep - mFlyLoopStep) * 0.05f) / TWO_PI;
    mSwingAngle = 0.0f;
    mSwingAngleMax = TWO_PI * temp2;
}

void SuperSpinDriver::addSwingSignRotateY() {
    if (isSwingOr2PTrigger()) {
        mSpinSpeed += 0.1f;

        if (mSpinSpeed > 0.23f) {
            mSpinSpeed = 0.23f;
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
    switch (mColor) {
    case 1:
        return true;
    default:
        return false;
    }
}

bool SuperSpinDriver::isRightToUse() const {
    switch (mColor) {
    case 1:
        return MR::isOnGameEventFlagGreenDriver();
    default:
        return true;
    }
}

bool SuperSpinDriver::isNerveEnableBind() const {
    return isNerve(GET_NERVE(SuperSpinDriver, SuperSpinDriverNrvWait)) && MR::isGreaterStep(this, ::sCanBindTime);
}

bool SuperSpinDriver::isSwingOr2PTrigger() const {
    return MR::isSpin(PLAYER_1) || MR::isPlayerPointedBy2POnTriggerButton();
}

bool SuperSpinDriver::isFarPlayer() const {
    return !MR::isNearPlayerAnyTime(this, 350.0f);
}

namespace MR {
    NameObj* createSuperSpinDriverYellow(const char* pName) {
        return new SuperSpinDriver(pName, 0);
    }

    NameObj* createSuperSpinDriverGreen(const char* pName) {
        return new SuperSpinDriver(pName, 1);
    }

    NameObj* createSuperSpinDriverPink(const char* pName) {
        return new SuperSpinDriver(pName, 2);
    }
};  // namespace MR
