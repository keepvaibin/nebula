#include "Game/MapObj/SpinDriver.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/SpinDriverCamera.hpp"
#include "Game/MapObj/SpinDriverShootPath.hpp"
#include "Game/MapObj/SpinDriverUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sSensorRadius = 300.0f;
    const f32 sClippingRadius = 300.0f;
    // 350 and 240 squared.
    const f32 sFarPlayerDistanceSq = 122500.0f;
    const f32 sPullPlayerDistanceSq = 57600.0f;
    const f32 sBindSpeedMax = 40.0f;
    const s32 sCaptureFrame = 40;
    const f32 sCaptureEndDistance = 15.0f;
    const f32 sCapturePullRate = 1.5f;
    const f32 sCapturePullDistance = 120.0f;
    const f32 sCaptureDamp = 0.8f;
    const s32 sShootStartFrame = 20;
    const f32 sShootStartFrameF = 20.0f;
    const s32 sShootStartSoundStep = 4;
    // The player dips this far below the driver at the start of the spin, back up by the end.
    const f32 sShootStartDip = 200.0f;
    const s32 sCoolDownFrame = 60;
    const s32 sCoolDownWaitStep = NebulaFrameClock::stepsOfSeconds(1);
    const f32 sSpinDamp = 0.95f;
    const f32 sSwingSpinAccel = 0.05f;
    const f32 sNonActiveSpinMax = 0.15f;
    const f32 sCoolDownSpinAccel = 0.008f;
    const f32 sCaptureSpinAccel = 0.008f;
    const f32 sShootStartSpinAccel = 0.04f;
    const f32 sHeadTurnRate = 0.4f;
    // The last frames of the flight are the fall; the short loop starts a fifth of the way to the fly end.
    const s32 sFallFrame = 20;
    const s32 sFlyShortDivide = 5;
    const s32 sDefaultSpaceFlyEndFrame = 50;
    const char* const cDemoNameAppear = "出現";
    const char* const cLightEffect = "SpinDriverLight";
}  // namespace

namespace NrvSpinDriver {
    NEW_NERVE(SpinDriverNrvTryDemo, SpinDriver, TryDemo);
    NEW_NERVE(SpinDriverNrvNonActive, SpinDriver, NonActive);
    NEW_NERVE(SpinDriverNrvAppear, SpinDriver, Appear);
    NEW_NERVE(SpinDriverNrvWait, SpinDriver, Wait);
    NEW_NERVE(SpinDriverNrvCapture, SpinDriver, Capture);
    NEW_NERVE(SpinDriverNrvShootStart, SpinDriver, ShootStart);
    NEW_NERVE(SpinDriverNrvShoot, SpinDriver, Shoot);
    NEW_NERVE(SpinDriverNrvCoolDown, SpinDriver, CoolDown);
};  // namespace NrvSpinDriver

SpinDriver::SpinDriver(const char* pName)
    : LiveActor(pName), mBindActor(), mShootPath(), mSpinDriverCamera(), mInitBindQuat(0, 0, 0, 1), mBindQuat(0, 0, 0, 1), mBindPos(0, 0, 0),
      mBindVelocity(0, 0, 0), mFront(0, 0, 1), mSide(0, 0, 0), mUp(0, 1, 0), mCaptureStartPos(0, 0, 0), mJumpSpeed(40.0f), mSpinAngle(), mSpinSpeed(),
      mClippingCenter(0, 0, 0), mClippingRadius(), mCaptureRate(), mShadowDropLength(-1.0f), mShootTime(300), mFlyShortStep(-1), mFlyEndStep(240),
      mFallStep(270), mSpaceFlyEndFrame(::sDefaultSpaceFlyEndFrame), mInitPoseAngle(), mCanBind(true), mCoolDown(), mIsCoolingDown(),
      mIsPlayerAway(true), mIsAppearSilent() {
    lumacore::setClass(this, lumacore::kClassId< SpinDriver >);
}

void SpinDriver::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("SpinDriver", nullptr, false);

    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);

    initShootPath(rIter);

    MR::offCalcGravity(this);

    Vec3f gravity;
    MR::calcGravityVectorOrZero(this, &gravity, nullptr, 0);

    if (MR::isNearZero(gravity)) {
        gravity.set< f32 >(0.0f, -1.0f, 0.0f);
    }

    mGravity.set(gravity);

    initHitSensor(1);
    MR::addHitSensor(this, "body", ATYPE_SPIN_DRIVER_BIND, 16, ::sSensorRadius, Vec3f(0.0f, 0.0f, 0.0f));

    initEffectKeeper(0, nullptr);
    initSound(6, false);
    initEventCamera(rIter);
    initNerve(GET_NERVE(SpinDriver, SpinDriverNrvWait));

    MR::initShadowVolumeFlatModel(this, "SpinDriverShadow", MR::getJointMtx(this, "SpinDriver"));

    initParamFromPlacement(rIter);

    MR::setClippingFar200m(this);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
        mSpinDriverCamera->initAppearCamera(rIter, this);
    } else {
        makeActorAppeared();
    }

    MR::useStageSwitchSleep(this, rIter);
}

void SpinDriver::initAfterPlacement() {
    SpinDriverUtil::setShadowAndClipping(this, &mClippingCenter, mShadowDropLength, ::sClippingRadius, &mClippingRadius);
}

void SpinDriver::initParamFromPlacement(const TableIter& rIter) {
    if (mShootPath == nullptr) {
        MR::getPlacementArg0NoInit(rIter, &mJumpSpeed);
    } else {
        MR::getPlacementArg0NoInit(rIter, &mShootTime);
    }

    mShadowDropLength = -1.0f;
    MR::getPlacementArg1NoInit(rIter, &mShadowDropLength);

    s32 canBind = 0;

    if (MR::getPlacementArg2NoInit(rIter, &canBind)) {
        mCanBind = canBind != 0;
    }

    s32 isAppearSilent = 0;

    if (MR::getPlacementArg4NoInit(rIter, &isAppearSilent)) {
        mIsAppearSilent = isAppearSilent == 1;
    }
}

void SpinDriver::initShootPath(const TableIter& rIter) {
    if (!MR::isConnectedWithRail(rIter)) {
        Pos3f rotateMtx;
        MR::makeMtxRotate(rotateMtx, mRotation);
        rotateMtx.getXYZDir(mSide, mUp, mFront);
        return;
    }

    mShootPath = new SpinDriverShootPath();

    mInitPoseAngle = 0.0f;
    MR::getPlacementArg5NoInit(rIter, &mInitPoseAngle);

    mShootPath->initUsingParabolic(rIter, mPosition);
    mShootPath->calcInitPose(&mUp, &mFront, &mSide, mInitPoseAngle);
}

void SpinDriver::initEventCamera(const TableIter& rIter) {
    mSpinDriverCamera = new SpinDriverCamera();
    mSpinDriverCamera->init(rIter, this);
}

void SpinDriver::appear() {
    LiveActor::appear();

    MR::invalidateClipping(this);

    if (mSpinDriverCamera->isUseAppearCamera(this)) {
        MR::requestStartDemo(this, ::cDemoNameAppear, GET_NERVE(SpinDriver, SpinDriverNrvAppear), GET_NERVE(SpinDriver, SpinDriverNrvTryDemo));
    } else {
        setNerve(GET_NERVE(SpinDriver, SpinDriverNrvAppear));
    }
}

void SpinDriver::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    setNerve(GET_NERVE(SpinDriver, SpinDriverNrvWait));
}

void SpinDriver::makeActorDead() {
    if (mBindActor != nullptr) {
        cancelBind();
    }

    LiveActor::makeActorDead();
}

void SpinDriver::control() {
    if (!mIsPlayerAway && mPosition.squared(*MR::getPlayerPos()) > ::sFarPlayerDistanceSq) {
        mIsPlayerAway = true;
    }

    if (mCoolDown > 0) {
        mCoolDown--;
    }

    mIsCoolingDown = mCoolDown > 0;

    mSpinAngle = MR::repeat(mSpinAngle + mSpinSpeed, -MR::pi(), 2 * MR::pi());
    mSpinSpeed *= ::sSpinDamp;
}

void SpinDriver::calcAndSetBaseMtx() {
    Pos3f baseMtx;
    MR::makeMtxUpFrontPos(&baseMtx, mUp, mFront, mPosition);

    Pos3f spinMtx;
    spinMtx.identity();
    spinMtx.setEulerY(mSpinAngle);

    baseMtx.concat(baseMtx, spinMtx);
    MR::setBaseTRMtx(this, baseMtx);
}

bool SpinDriver::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    switch (msg) {
    case ACTMES_IS_RUSH_TAKEOVER:
        mCoolDown = ::sCoolDownFrame;
        return canBind(pSender);
    case ACTMES_AUTORUSH_BEGIN:
        mCoolDown = ::sCoolDownFrame;
        return startBind(pSender);
    case ACTMES_RUSH_CANCEL:
        if (isNerve(GET_NERVE(SpinDriver, SpinDriverNrvShootStart))) {
            return false;
        }

        return tryReleaseBind();
    case ACTMES_RUSH_FORCE_CANCEL:
        return tryReleaseBind();
    case ACTMES_UPDATE_BASEMTX:
        if (mBindActor == nullptr) {
            return false;
        }

        calcBindActorMatrix();
        return true;
    }

    return false;
}

bool SpinDriver::tryReleaseBind() {
    if (mBindActor == nullptr) {
        return false;
    }

    mBindActor = nullptr;
    cancelCamera();
    return true;
}

void SpinDriver::startSpinAttackSound(LiveActor* pPlayer) {
    MR::startSound(pPlayer, "SE_PM_SPIN_ATTACK");

    if (MR::isInAreaObj("Water", mPosition)) {
        MR::startSound(this, "SE_PM_SPIN_DRV_IN_WATER_1");
    }
}

bool SpinDriver::tryStartShoot() {
    if (!isSwingOr2PTrigger()) {
        return false;
    }

    startSpinAttackSound(mBindActor);
    setNerve(GET_NERVE(SpinDriver, SpinDriverNrvShootStart));
    return true;
}

bool SpinDriver::tryEndCapture() {
    if (MR::isGreaterStep(this, ::sCaptureFrame) && mBindPos.distance(mPosition) < ::sCaptureEndDistance) {
        cancelBind();
        mIsPlayerAway = false;
        setNerve(GET_NERVE(SpinDriver, SpinDriverNrvWait));
        return true;
    }

    return false;
}

bool SpinDriver::tryShoot() {
    if (MR::isGreaterStep(this, ::sShootStartFrame)) {
        setNerve(GET_NERVE(SpinDriver, SpinDriverNrvShoot));
        return true;
    }

    return false;
}

// Without a rail the launch lasts one step.
bool SpinDriver::tryEndShoot() {
    if (mShootPath == nullptr || MR::isGreaterStep(this, mShootTime)) {
        MR::endBindAndSpinDriverJump(this, mBindVelocity);
        mBindActor = nullptr;
        endCamera();
        setNerve(GET_NERVE(SpinDriver, SpinDriverNrvCoolDown));
        return true;
    }

    return false;
}

bool SpinDriver::tryForceCancel() {
    if (mBindActor == nullptr) {
        setNerve(GET_NERVE(SpinDriver, SpinDriverNrvCoolDown));
        return true;
    }

    return false;
}

bool SpinDriver::tryEndCoolDown() {
    if (MR::isGreaterStep(this, ::sCoolDownWaitStep)) {
        setNerve(GET_NERVE(SpinDriver, SpinDriverNrvWait));
        return true;
    }

    return false;
}

bool SpinDriver::trySwitchOff() {
    if (MR::isValidSwitchAppear(this) && !MR::isOnSwitchAppear(this)) {
        kill();
        return true;
    }

    return false;
}

void SpinDriver::startWaitAnim() {
    MR::validateClipping(this);
    MR::startBck(this, "Wait");
    MR::startBpk(this, "Wait");
}

// Waits in the demo request until the demo starts the appear nerve.
void SpinDriver::exeTryDemo() {
}

void SpinDriver::exeNonActive() {
    if (MR::isFirstStep(this)) {
        startWaitAnim();
    }

    if (isSwingOr2PTrigger()) {
        mSpinSpeed += ::sSwingSpinAccel;

        if (mSpinSpeed > ::sNonActiveSpinMax) {
            mSpinSpeed = ::sNonActiveSpinMax;
        }
    }
}

void SpinDriver::exeAppear() {
    if (MR::isFirstStep(this)) {
        if (!mIsAppearSilent) {
            MR::startSystemSE("SE_SY_SPIN_DRIVER_APPEAR");
            MR::startSound(this, "SE_OJ_SPIN_DRV_APPEAR");
        }

        MR::startBck(this, "Appear");
        mSpinDriverCamera->startAppearCamera(this, mUp, mFront, mPosition);
    }

    if (MR::isBckStopped(this) && MR::isGreaterStep(this, mSpinDriverCamera->getAppearCameraFrames())) {
        setNerve(GET_NERVE(SpinDriver, SpinDriverNrvWait));

        if (mSpinDriverCamera->isUseAppearCamera(this)) {
            MR::endDemoWaitCameraInterpolating(this, ::cDemoNameAppear);
            mSpinDriverCamera->endAppearCamera(this);
        }
    }
}

void SpinDriver::exeWait() {
    if (MR::isFirstStep(this)) {
        startWaitAnim();
    }

    if (isSwingOr2PTrigger()) {
        mSpinSpeed += ::sSwingSpinAccel;
    }

    // A player just bound or rushed in: spin up and shine, and light up on the first frame of it.
    if (mCoolDown > 0) {
        mSpinSpeed += ::sCoolDownSpinAccel;

        MR::startLevelSound(this, "SE_OJ_LV_SPIN_DRV_SHINE");

        if (!mIsCoolingDown) {
            MR::emitEffect(this, ::cLightEffect);
            MR::startCSSound("CS_SPIN_BIND", nullptr, 0);
        }
    } else {
        MR::deleteEffect(this, ::cLightEffect);
    }

    trySwitchOff();
}

void SpinDriver::exeCapture() {
    if (tryForceCancel()) {
        MR::deleteEffect(this, ::cLightEffect);
        return;
    }

    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("SpinDriverWait", "SpinDriverCapture");
        MR::startBck(this, "Active");
        MR::emitEffect(this, ::cLightEffect);
    }

    MR::startLevelSound(this, "SE_OJ_LV_SPIN_DRV_SHINE");
    MR::startLevelSound(this, "SE_OJ_LV_SPIN_DRV_CAPTURE");

    moveBindPosToCenter();

    mCaptureRate = MR::calcNerveRate(this, ::sCaptureFrame);
    updateBindActorMatrix(mCaptureRate);

    mSpinSpeed += ::sCaptureSpinAccel;

    MR::tryRumblePadWeak(this, PLAYER_1);

    mCoolDown = ::sCoolDownFrame;

    if (!tryStartShoot()) {
        tryEndCapture();
    }
}

void SpinDriver::exeShootStart() {
    if (tryForceCancel()) {
        return;
    }

    if (MR::isFirstStep(this)) {
        mCaptureStartPos = mBindPos;

        MR::deleteEffect(this, ::cLightEffect);
        MR::emitEffect(this, "SpinDriverStart");

        MR::startBckPlayer("Spin", "SpinDriverShoot");
        MR::startBck(this, "Active");
        MR::startBpk(this, "Active");

        startCamera();
    }

    if (MR::isStep(this, ::sShootStartSoundStep)) {
        MR::startCSSound("CS_SPIN_DRIVE_SHORT", "SE_SY_CS_SPIN_DRV_START", 0);
    }

    // The dip is (2p - 1)^4 deep; the player reaches the dip point by half way.
    f32 progress = MR::clamp(getNerveStep() / ::sShootStartFrameF, 0.0f, 1.0f);
    f32 half = 2.0f * (progress - 0.5f);
    f32 square = half * half;
    f32 dip = square * square;

    Vec3f up;
    MR::calcUpVec(&up, this);

    Vec3f dipPos;
    dipPos.set(mPosition - up * ::sShootStartDip * (1.0f - dip));

    f32 moveRate = MR::clamp(2.0f * progress, 0.0f, 1.0f);
    mBindPos.set(mCaptureStartPos * (1.0f - moveRate) + dipPos * moveRate);

    updateBindActorMatrix(moveRate + mCaptureRate * (1.0f - moveRate));

    mSpinSpeed += ::sShootStartSpinAccel;

    MR::tryRumblePadMiddle(this, PLAYER_1);
    tryShoot();
}

void SpinDriver::exeShoot() {
    if (tryForceCancel()) {
        return;
    }

    if (MR::isFirstStep(this)) {
        calcShootMotionTime();

        if (MR::hasME()) {
            MR::startSystemME("ME_MAGIC");
        } else {
            MR::startSystemSE("SE_SY_S_SPIN_DRV_ME_ALT");
        }

        MR::startSound(this, "SE_OJ_SPIN_DRV_JUMP");
        MR::startSound(mBindActor, "SE_PV_JUMP_JOY");

        if (MR::isInAreaObj("Water", mPosition)) {
            MR::startSound(this, "SE_PM_SPIN_DIV_IN_WATER_2");
        }

        MR::startBckPlayer("SpaceFlyShortStart", "SpinDriverFlyStart");

        MR::shakeCameraNormal();
        MR::tryRumblePadVeryStrong(this, PLAYER_1);
    }

    if (mShootPath != nullptr) {
        updateBindPosition();
        updateCamera();

        calcParabolicBindPose();

        if (MR::isStep(this, mFlyShortStep)) {
            MR::startBckPlayer("SpaceFlyShort", "SpinDriverFlyLoop");
        }

        if (MR::isStep(this, mFlyEndStep)) {
            MR::startBckPlayer("SpaceFlyEnd", "SpinDriverFlyEnd");
        }

        if (MR::isLessStep(this, mFallStep)) {
            MR::startLevelSound(mBindActor, "SE_PM_LV_SPIN_DRV_FLY");
        }

        if (MR::isStep(this, mFallStep)) {
            MR::startBckPlayer("Fall", "SpinDriverFall");
        }
    } else {
        mBindVelocity = mUp * mJumpSpeed;
    }

    tryEndShoot();
}

void SpinDriver::exeCoolDown() {
    if (MR::isFirstStep(this)) {
        MR::startBpk(this, "Wait");
    }

    if (!tryEndCoolDown()) {
        trySwitchOff();
    }
}

bool SpinDriver::startBind(HitSensor* pSensor) {
    if (!canStartBind()) {
        return false;
    }

    if (isSwingOr2PTrigger()) {
        if (MR::hasME()) {
            MR::startSystemME("ME_MAGIC");
        } else {
            MR::startSystemSE("SE_SY_S_SPIN_DRV_ME_ALT");
        }

        startSpinAttackSound(pSensor->mHost);
        setNerve(GET_NERVE(SpinDriver, SpinDriverNrvShootStart));
    } else if (isPullPlayer(pSensor)) {
        setNerve(GET_NERVE(SpinDriver, SpinDriverNrvCapture));
    } else {
        return false;
    }

    mBindActor = pSensor->mHost;
    mBindPos = mBindActor->mPosition;

    mBindVelocity = *MR::getPlayerLastMove();

    f32 speed = mBindVelocity.length();

    if (speed > ::sBindSpeedMax) {
        mBindVelocity *= ::sBindSpeedMax / speed;
    }

    mCaptureRate = 0.0f;

    Rot3f rotation;
    rotation.set(mBindActor->getBaseMtx());
    rotation.getQuat(mInitBindQuat);

    mBindQuat = mInitBindQuat;

    mSpaceFlyEndFrame = MR::getBckFrameMax(mBindActor, "SpaceFlyEnd");

    MR::invalidateClipping(this);
    return true;
}

void SpinDriver::cancelBind() {
    if (mBindActor != nullptr) {
        MR::endBindAndPlayerJump(this, mBindVelocity, 0);
        mBindActor = nullptr;
        endCamera();
    }
}

void SpinDriver::updateBindPosition() {
    // A shoot time of 0 or less (placement arg 0) is at the path's end.
    f32 coord = mShootTime > 0 ? static_cast< f32 >(getNerveStep()) / mShootTime : 1.0f;

    Vec3f lastPos(mBindPos);

    Vec3f position;
    mShootPath->calcPosition(&position, coord);

    mBindPos = position;
    mBindVelocity = position - lastPos;
}

void SpinDriver::moveBindPosToCenter() {
    mBindPos += mBindVelocity;

    f32 dist;
    Vec3f toCenter(mPosition - mBindPos);
    MR::separateScalarAndDirection(&dist, &toCenter, toCenter);

    mBindVelocity += toCenter * ::sCapturePullRate * (dist / ::sCapturePullDistance);
    mBindVelocity.x *= ::sCaptureDamp;
    mBindVelocity.y *= ::sCaptureDamp;
    mBindVelocity.z *= ::sCaptureDamp;
}

void SpinDriver::updateBindActorMatrix(f32 rate) {
    Pos3f shootMtx;
    MR::makeMtxUpFrontPos(&shootMtx, mUp, mFront, mPosition);

    Quat4f shootQuat;
    shootMtx.getQuat(shootQuat);

    mBindQuat.slerp(mInitBindQuat, shootQuat, rate);
}

void SpinDriver::calcBindActorMatrix() {
    Pos3f baseMtx;
    baseMtx.setQT(mBindQuat, mBindPos);
    MR::setBaseTRMtx(mBindActor, baseMtx);
}

// Head along the velocity, rolled over about the side axis from the fly end to the fall.
void SpinDriver::calcParabolicBindPose() {
    if (MR::isNearZero(mBindVelocity)) {
        return;
    }

    Vec3f direction;
    MR::normalize(mBindVelocity, &direction);

    if (mShootTime > 0) {
        f32 rate = MR::calcNerveRate(this, mShootTime);
        rate = MR::normalize(rate, static_cast< f32 >(mFlyEndStep) / mShootTime, static_cast< f32 >(mFallStep) / mShootTime);

        if (rate > 0.0f) {
            Quat4f roll;
            const f32 angle = PI * rate;
            roll.setRotate(mSide, angle);
            roll.rotate(direction);
        }
    }

    turnBindHead(direction, ::sHeadTurnRate);
}

void SpinDriver::turnBindHead(const Vec3f& rUp, f32 rate) {
    Vec3f bindUp;
    mBindQuat.getYDir(bindUp);

    Quat4f turn;
    turn.setRotate(bindUp, rUp, rate);
    mBindQuat.mult(turn);
    mBindQuat.normalize();
}

void SpinDriver::calcShootMotionTime() {
    if (mShootTime < mSpaceFlyEndFrame + ::sFallFrame) {
        mFlyShortStep = -1;
        mFlyEndStep = 0;
        mFallStep = mShootTime;
    } else {
        mFlyEndStep = mShootTime - (mSpaceFlyEndFrame + ::sFallFrame);
        mFallStep = mShootTime - ::sFallFrame;
        mFlyShortStep = mFlyEndStep / ::sFlyShortDivide;
    }
}

void SpinDriver::startCamera() {
    if (mSpinDriverCamera != nullptr) {
        mSpinDriverCamera->start(mUp, mFront, mPosition);
    }
}

void SpinDriver::updateCamera() {
    if (mSpinDriverCamera != nullptr) {
        mSpinDriverCamera->update(mBindVelocity, mBindPos);
    }
}

void SpinDriver::cancelCamera() {
    if (mSpinDriverCamera != nullptr) {
        mSpinDriverCamera->cancel();
    }
}

void SpinDriver::endCamera() {
    if (mSpinDriverCamera != nullptr) {
        mSpinDriverCamera->end();
    }
}

bool SpinDriver::canStartBind() const {
    return isNerve(GET_NERVE(SpinDriver, SpinDriverNrvWait));
}

bool SpinDriver::isSwingOr2PTrigger() const {
    return MR::isSpin(PLAYER_1) || MR::isPlayerPointedBy2POnTriggerButton();
}

// A player who walked away is pulled in when back in range.
bool SpinDriver::isPullPlayer(const HitSensor* pSensor) const {
    return mCanBind && mIsPlayerAway && mPosition.squared(pSensor->mPosition) < ::sPullPlayerDistanceSq;
}

bool SpinDriver::canBind(HitSensor* pSensor) const {
    return canStartBind() && (isSwingOr2PTrigger() || isPullPlayer(pSensor));
}
