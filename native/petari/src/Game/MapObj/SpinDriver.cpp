#include "Game/MapObj/SpinDriver.hpp"
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
#include <cmath>

#include "Game/Util/StringUtil.hpp"

namespace {
    static s32 cSpaceFlyStartFrame = 10;
    static s32 cSpaceFlyEndFrame = 50;
};  // namespace

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
    : LiveActor(pName), mBindActor(), mShootPath(), mSpinDriverCamera(), mInitBindQuat(0, 0, 0, 1), mBindQuat(0, 0, 0, 1), mBindPos(0, 0, 0), mBindVelocity(0, 0, 0), mFront(0, 0, 1),
      mSide(0, 0, 0), mUp(0, 1, 0), mCaptureStartPos(0, 0, 0), mSpinAngle(), mJumpSpeed(40.0f), mSpinSpeed(), mClippingCenter(0, 0, 0), mCaptureRate(), mShadowDropLength(-1.0f), mShootTime(300), mFlyShortStep(-1), mFlyEndStep(240),
      mFallStep(270), mInitPoseAngle(), mCanBind(true), mCoolDown(), mIsCoolingDown(), mIsPlayerAway(true), mIsAppearSilent() {
}

void SpinDriver::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("SpinDriver", nullptr, false);

    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);

    initShootPath(rIter);

    MR::offCalcGravity(this);

    Vec3f gravityVector;
    MR::calcGravityVectorOrZero(this, &gravityVector, nullptr, 0);

    if (MR::isNearZero(gravityVector)) {
        gravityVector.set< f32 >(0.0f, -1.0f, 0.0f);
    }

    mGravity.set(gravityVector);

    initHitSensor(1);
    MR::addHitSensor(this, "body", ATYPE_SPIN_DRIVER_BIND, 16, 300.0f, Vec3f(0.0f, 0.0f, 0.0f));

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
    SpinDriverUtil::setShadowAndClipping(this, &mClippingCenter, mShadowDropLength, 300.0f, &mClippingRadius);
}

void SpinDriver::initParamFromPlacement(const TableIter& rIter) {
    if (mShootPath == nullptr) {
        MR::getPlacementArg0NoInit(rIter, &mJumpSpeed);
    } else {
        MR::getPlacementArg0NoInit(rIter, &mShootTime);
    }

    mShadowDropLength = -1.0f;
    MR::getPlacementArg1NoInit(rIter, &mShadowDropLength);

    s32 arg2 = 0;

    if (MR::getPlacementArg2NoInit(rIter, &arg2)) {
        mCanBind = arg2 != 0;
    }

    s32 arg4 = 0;

    if (MR::getPlacementArg4NoInit(rIter, &arg4)) {
        mIsAppearSilent = !(arg4 - 1);
    }
}

void SpinDriver::initShootPath(const TableIter& rIter) {
    if (MR::isConnectedWithRail(rIter)) {
        mShootPath = new SpinDriverShootPath();

        mInitPoseAngle = 0.0f;
        MR::getPlacementArg5NoInit(rIter, &mInitPoseAngle);

        mShootPath->initUsingParabolic(rIter, mPosition);
        mShootPath->calcInitPose(&mUp, &mFront, &mSide, mInitPoseAngle);
    } else {
        Pos3f rotateMtx;
        MR::makeMtxRotate(rotateMtx, mRotation);
        rotateMtx.getXYZDir(mSide, mUp, mFront);
    }
}

void SpinDriver::initEventCamera(const TableIter& rIter) {
    mSpinDriverCamera = new SpinDriverCamera();
    mSpinDriverCamera->init(rIter, this);

    if (MR::isConnectedWithRail(rIter)) {
    }
}

void SpinDriver::appear() {
    LiveActor::appear();

    MR::invalidateClipping(this);

    if (mSpinDriverCamera->isUseAppearCamera(this)) {
        MR::requestStartDemo(this, "出現", GET_NERVE(SpinDriver, SpinDriverNrvAppear), GET_NERVE(SpinDriver, SpinDriverNrvTryDemo));
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
    if (!mIsPlayerAway && mPosition.squared(*MR::getPlayerPos()) > 122500.0f) {
        mIsPlayerAway = true;
    }

    if (mCoolDown > 0) {
        mCoolDown--;
    }

    mIsCoolingDown = mCoolDown > 0;

    mSpinAngle += mSpinSpeed;
    mSpinAngle = MR::repeat(mSpinAngle, -MR::pi(), 2 * MR::pi());

    mSpinSpeed *= 0.95f;
}

void SpinDriver::calcAndSetBaseMtx() {
    Pos3f baseMtx;
    MR::makeMtxUpFrontPos(&baseMtx, mUp, mFront, mPosition);

    Pos3f yMtx;
    yMtx.identity();
    yMtx.setEulerY(mSpinAngle);

    baseMtx.concat(baseMtx, yMtx);
    MR::setBaseTRMtx(this, baseMtx);
}

bool SpinDriver::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg == ACTMES_IS_RUSH_TAKEOVER) {
        mCoolDown = 60;
        return canBind(pSender);
    }

    if (msg == ACTMES_AUTORUSH_BEGIN) {
        mCoolDown = 60;

        if (startBind(pSender)) {
            return true;
        }
    }

    if (msg == ACTMES_RUSH_CANCEL) {
        if (isNerve(GET_NERVE(SpinDriver, SpinDriverNrvShootStart))) {
            return false;
        }

        if (mBindActor != nullptr) {
            mBindActor = nullptr;

            cancelCamera();

            return true;
        }
    }

    if (msg == ACTMES_RUSH_FORCE_CANCEL && mBindActor != nullptr) {
        mBindActor = nullptr;

        cancelCamera();

        return true;
    }

    if (msg == ACTMES_UPDATE_BASEMTX && mBindActor != nullptr) {
        calcBindActorMatrix();
        return true;
    }

    return false;
}

bool SpinDriver::tryStartShoot() {
    bool isSwingOr2P = MR::isSpin(PLAYER_1) || MR::isPlayerPointedBy2POnTriggerButton();

    if (isSwingOr2P) {
        MR::startSound(mBindActor, "SE_PM_SPIN_ATTACK");

        if (MR::isInAreaObj("Water", mPosition)) {
            MR::startSound(this, "SE_PM_SPIN_DRV_IN_WATER_1");
        }

        setNerve(GET_NERVE(SpinDriver, SpinDriverNrvShootStart));

        return true;
    }

    return false;
}

bool SpinDriver::tryEndCapture() {
    if (MR::isGreaterStep(this, 40) && mBindPos.distance(mPosition) < 15.0f) {
        cancelBind();

        mIsPlayerAway = 0;

        setNerve(GET_NERVE(SpinDriver, SpinDriverNrvWait));

        return true;
    }

    return false;
}

bool SpinDriver::tryShoot() {
    if (MR::isGreaterStep(this, 20)) {
        setNerve(GET_NERVE(SpinDriver, SpinDriverNrvShoot));
        return true;
    }

    return false;
}

bool SpinDriver::tryEndShoot() {
    if (mShootPath == nullptr || MR::isGreaterStep(this, mShootTime)) {
        MR::endBindAndSpinDriverJump(this, mBindVelocity);

        mBindActor = 0;

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
    if (MR::isGreaterStep(this, 60)) {
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

void SpinDriver::exeTryDemo() {
}

void SpinDriver::exeNonActive() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);

        MR::startBck(this, "Wait");
        MR::startBpk(this, "Wait");
    }

    bool isSwingOr2P = MR::isSpin(PLAYER_1) || MR::isPlayerPointedBy2POnTriggerButton();

    if (isSwingOr2P) {
        mSpinSpeed += 0.05f;

        if (mSpinSpeed > 0.15f) {
            mSpinSpeed = 0.15f;
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
            MR::endDemoWaitCameraInterpolating(this, "出現");
            mSpinDriverCamera->endAppearCamera(this);
        }
    }
}

void SpinDriver::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);

        MR::startBck(this, "Wait");
        MR::startBpk(this, "Wait");
    }

    bool isSwingOr2P = MR::isSpin(PLAYER_1) || MR::isPlayerPointedBy2POnTriggerButton();

    if (isSwingOr2P) {
        mSpinSpeed += 0.05f;
    }

    if (mCoolDown > 0) {
        mSpinSpeed += 0.008f;

        MR::startLevelSound(this, "SE_OJ_LV_SPIN_DRV_SHINE");

        if (!mIsCoolingDown) {
            MR::emitEffect(this, "SpinDriverLight");
            MR::startCSSound("CS_SPIN_BIND", nullptr, 0);
        }
    } else {
        MR::deleteEffect(this, "SpinDriverLight");
    }

    if (trySwitchOff()) {
        return;
    }
}

void SpinDriver::exeCapture() {
    if (tryForceCancel()) {
        MR::deleteEffect(this, "SpinDriverLight");
        return;
    }

    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("SpinDriverWait", "SpinDriverCapture");
        MR::startBck(this, "Active");

        MR::emitEffect(this, "SpinDriverLight");
    }

    MR::startLevelSound(this, "SE_OJ_LV_SPIN_DRV_SHINE");
    MR::startLevelSound(this, "SE_OJ_LV_SPIN_DRV_CAPTURE");

    moveBindPosToCenter();

    mCaptureRate = MR::calcNerveRate(this, 40);

    updateBindActorMatrix(mCaptureRate);

    mSpinSpeed += 0.008f;

    MR::tryRumblePadWeak(this, PLAYER_1);

    mCoolDown = 60;

    if (!tryStartShoot() && tryEndCapture()) {
        return;
    }
}

void SpinDriver::exeShootStart() {
    if (tryForceCancel()) {
        return;
    }

    if (MR::isFirstStep(this)) {
        mCaptureStartPos = mBindPos;

        MR::deleteEffect(this, "SpinDriverLight");
        MR::emitEffect(this, "SpinDriverStart");

        MR::startBckPlayer("Spin", "SpinDriverShoot");
        MR::startBck(this, "Active");
        MR::startBpk(this, "Active");

        startCamera();
    }

    if (MR::isStep(this, 4)) {
        MR::startCSSound("CS_SPIN_DRIVE_SHORT", "SE_SY_CS_SPIN_DRV_START", 0);
    }

    f32 progress = MR::clamp(getNerveStep() / 20.0f, 0.0f, 1.0f);
    f32 half = 2.0f * (progress - 0.5f);
    f32 square = half * half;
    f32 val = square * square;

    Vec3f upVec;
    MR::calcUpVec(&upVec, this);

    Vec3f vec50;
    vec50.set(mPosition - upVec * 200.0f * (1.0f - val));

    f32 val2 = MR::clamp(2.0f * progress, 0.0f, 1.0f);
    mBindPos.set(mCaptureStartPos * (1.0f - val2) + vec50 * val2);

    updateBindActorMatrix(val2 + mCaptureRate * (1.0f - val2));

    mSpinSpeed += 0.04f;

    MR::tryRumblePadMiddle(this, 0);

    if (tryShoot()) {
        return;
    }
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

    if (tryEndShoot()) {
        return;
    }
}

void SpinDriver::exeCoolDown() {
    if (MR::isFirstStep(this)) {
        MR::startBpk(this, "Wait");
    }

    if (!tryEndCoolDown() && trySwitchOff()) {
        return;
    }
}

bool SpinDriver::startBind(HitSensor* pSensor) {
    if (!canStartBind()) {
        return false;
    }

    bool isSwingOr2P = MR::isSpin(PLAYER_1) || MR::isPlayerPointedBy2POnTriggerButton();

    if (isSwingOr2P) {
        if (MR::hasME()) {
            MR::startSystemME("ME_MAGIC");
        } else {
            MR::startSystemSE("SE_SY_S_SPIN_DRV_ME_ALT");
        }

        MR::startSound(pSensor->mHost, "SE_PM_SPIN_ATTACK");

        if (MR::isInAreaObj("Water", mPosition)) {
            MR::startSound(this, "SE_PM_SPIN_DRV_IN_WATER_1");
        }

        setNerve(GET_NERVE(SpinDriver, SpinDriverNrvShootStart));
    } else {
        if (mCanBind && mIsPlayerAway && mPosition.squared(pSensor->mPosition) < 57600.0f) {
            setNerve(GET_NERVE(SpinDriver, SpinDriverNrvCapture));
        } else {
            return false;
        }
    }

    mBindActor = pSensor->mHost;
    mBindPos = mBindActor->mPosition;

    mBindVelocity = *MR::getPlayerLastMove();

    f32 mag = mBindVelocity.length();

    if (mag > 40.0f) {
        mBindVelocity *= 40.0f / mag;
    }

    mCaptureRate = 0.0f;

    Rot3f rotation;
    rotation.set(mBindActor->getBaseMtx());
    rotation.getQuat(mInitBindQuat);

    mBindQuat = mInitBindQuat;

    ::cSpaceFlyStartFrame = MR::getBckFrameMax(mBindActor, "SpaceFlyStart");
    ::cSpaceFlyEndFrame = MR::getBckFrameMax(mBindActor, "SpaceFlyEnd");

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
    f32 progress = static_cast< f32 >(getNerveStep()) / mShootTime;

    Vec3f oldPos(mBindPos);

    Vec3f position;
    mShootPath->calcPosition(&position, progress);

    mBindPos = position;
    mBindVelocity = position - oldPos;
}

void SpinDriver::moveBindPosToCenter() {
    mBindPos += mBindVelocity;

    f32 mag;
    Vec3f dir(mPosition - mBindPos);
    MR::separateScalarAndDirection(&mag, &dir, dir);

    mBindVelocity += dir * 1.5f * (mag / 120.0f);
    mBindVelocity.x *= 0.8f;
    mBindVelocity.y *= 0.8f;
    mBindVelocity.z *= 0.8f;
}

void SpinDriver::updateBindActorMatrix(f32 a1) {
    Pos3f makeMtx;
    MR::makeMtxUpFrontPos(&makeMtx, mUp, mFront, mPosition);

    Quat4f quat;
    makeMtx.getQuat(quat);

    mBindQuat.slerp(mInitBindQuat, quat, a1);
}

void SpinDriver::calcBindActorMatrix() {
    Pos3f baseMtx;
    baseMtx.setQT(mBindQuat, mBindPos);
    MR::setBaseTRMtx(mBindActor, baseMtx);
}

void SpinDriver::calcParabolicBindPose() {
    if (!MR::isNearZero(mBindVelocity)) {
        Vec3f direction;
        MR::normalize(mBindVelocity, &direction);

        if (mShootTime > 0) {
            f32 rate = MR::calcNerveRate(this, mShootTime);
            rate = MR::normalize(rate, static_cast< f32 >(mFlyEndStep) / mShootTime, static_cast< f32 >(mFallStep) / mShootTime);

            if (rate > 0.0f) {
                Quat4f rotation;
                const f32 angle = PI * rate;
                rotation.setRotate(mSide, angle);
                rotation.rotate(direction);
            }
        }

        turnBindHead(direction, 0.4f);
    }
}

void SpinDriver::turnBindHead(const Vec3f& rVec, f32 f1) {
    Vec3f yDir;
    mBindQuat.getYDir(yDir);

    Quat4f q;
    q.setRotate(yDir, rVec, f1);
    mBindQuat.mult(q);
    mBindQuat.normalize();
}

void SpinDriver::calcShootMotionTime() {
    if (mShootTime < ::cSpaceFlyEndFrame + 20) {
        mFlyShortStep = -1;
        mFlyEndStep = 0;
        mFallStep = mShootTime;
    } else {
        mFlyEndStep = mShootTime - (::cSpaceFlyEndFrame + 20);
        mFallStep = mShootTime - 20;
        mFlyShortStep = mFlyEndStep / 5;
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

bool SpinDriver::canBind(HitSensor* pSensor) const {
    if (!canStartBind()) {
        return false;
    }

    bool isSwingOr2P = MR::isSpin(PLAYER_1) || MR::isPlayerPointedBy2POnTriggerButton();

    if (isSwingOr2P) {
        return true;
    }

    if (mCanBind && mIsPlayerAway && mPosition.squared(pSensor->mPosition) < 57600.0f) {
        return true;
    }

    return false;
}

