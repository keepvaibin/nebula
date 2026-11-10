#include "Game/Map/SphereSelectorHandle.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/SphereSelector.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_playRotateSE_f1, "playRotateSE.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_playRotateSE_f2, "playRotateSE.f2", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_playRotateSE_f3, "playRotateSE.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_playRotateSE_f4, "playRotateSE.f4", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_playRotateSE_f5, "playRotateSE.f5", 1.6f);

namespace {
    NEBULA_TUNABLE(s32, cHoldReduceOutScreenFrame, 5);
    NEBULA_TUNABLE(f32, cRotateSpeedChangeMax, 0.2f);
    NEBULA_TUNABLE(f32, cHoldReduceRate, 0.9f);
    NEBULA_TUNABLE(f32, cSpinReduceRate, 0.95f);
    NEBULA_TUNABLE(f32, cRotateSpeedMaxY, 5.0f);
    NEBULA_TUNABLE(f32, cHoldRotateRateY, 0.2f);
    NEBULA_TUNABLE(s32, cDemoRotateStartFrame, 60);
    NEBULA_TUNABLE(f32, cDemoRotateSpeedY, 0.03f);
    NEBULA_TUNABLE(f32, cRotateSpeedMaxX, 2.0f);
    NEBULA_TUNABLE(f32, cHoldRotateRateX, 0.075f);
    NEBULA_TUNABLE(f32, cRotateAppearX, 40.0f);
    NEBULA_TUNABLE(f32, cRotateMinX, -15.0f);
    NEBULA_TUNABLE(f32, cRotateMaxX, 80.0f);
    NEBULA_TUNABLE(s32, cBgmAppearState, 2);
    NEBULA_TUNABLE(s32, cBgmAppearFrames, 60);
    NEBULA_TUNABLE(s32, cBgmDisappearState, 1);
    NEBULA_TUNABLE(s32, cBgmDisappearFrames, 90);
    NEBULA_TUNABLE(s32, cBgmRotateState, 4);
    NEBULA_TUNABLE(s32, cBgmRotateFrames, 60);
    NEBULA_TUNABLE(s32, cBgmNotRotateState, 3);
    NEBULA_TUNABLE(s32, cBgmNotRotateFrames, 30);
    NEBULA_TUNABLE(s32, cBgmConfirmState, 6);
    NEBULA_TUNABLE(s32, cBgmConfirmFrames, 60);
    NEBULA_TUNABLE(s32, cBgmNotConfirmState, 5);
    NEBULA_TUNABLE(s32, cBgmNotConfirmFrames, 60);
};  // namespace

namespace NrvSphereSelectorHandle {
    NEW_NERVE(SphereSelectorHandleNrvWait, SphereSelectorHandle, Wait);
    NEW_NERVE(SphereSelectorHandleNrvHold, SphereSelectorHandle, Hold);
    NEW_NERVE(SphereSelectorHandleNrvSpin, SphereSelectorHandle, Spin);
    NEW_NERVE(SphereSelectorHandleNrvDemoRotate, SphereSelectorHandle, DemoRotate);
    NEW_NERVE(SphereSelectorHandleNrvDisappear, SphereSelectorHandle, Disappear);
    NEW_NERVE(SphereSelectorHandleNrvInvalidRotate, SphereSelectorHandle, InvalidRotate);
    NEW_NERVE(SphereSelectorHandleNrvGalaxyConfirmStart, SphereSelectorHandle, GalaxyConfirmStart);
    NEW_NERVE(SphereSelectorHandleNrvGalaxyConfirmWait, SphereSelectorHandle, GalaxyConfirmWait);
    NEW_NERVE(SphereSelectorHandleNrvGalaxyConfirmCancel, SphereSelectorHandle, GalaxyConfirmCancel);
    NEW_NERVE(SphereSelectorHandleNrvIdleForFileSelect, SphereSelectorHandle, IdleForFileSelect);
    NEW_NERVE(SphereSelectorHandleNrvIdleEndForFileSelect, SphereSelectorHandle, IdleEndForFileSelect);
};  // namespace NrvSphereSelectorHandle

SphereSelectorHandle::SphereSelectorHandle(const char* pName)
    : LiveActor(pName), mIsFileSelectMode(), mFront(0.0f, 0.0f, -1.0f), mRotateSpeed(), mTargetRotateSpeed(), mRotateSpeedX(), mPrevRotateSpeedX(), mSide(1.0f, 0.0f, 0.0f),
      mUp(0.0f, 1.0f, 0.0f), mPointerVelocities(), mNotPointingStep(), mIsRotateBgm() {
    clearPointerVelocity();
    mBaseMtx.identity();
}

void SphereSelectorHandle::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_Environment, MR::CalcAnimType_MapObj, MR::DrawBufferType_None, MR::DrawType_None);
    MR::getPlacementArg0NoInit(rIter, &mIsFileSelectMode);
    MR::invalidateClipping(this);
    initNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvWait));
    MR::tryRegisterDemoCast(this, rIter);
    MR::registerDemoSimpleCastAll(this);
    SphereSelectorFunction::registerTarget(this);
    SphereSelectorFunction::setHandle(this);
    makeActorDead();
}

void SphereSelectorHandle::appear() {
    LiveActor::appear();
    mFront.negate(MR::getCamZdir());
    mFront.y = 0.0f;
    MR::normalize(&mFront);
    resetRotateParam();
    mNotPointingStep = 0;
    clearPointerVelocity();
    mRotation.zero();
    mSide.cross(Vec3f(0.0f, 1.0f, 0.0f), mFront);
    MR::rotateVecDegree(&mUp, Vec3f(0.0f, 1.0f, 0.0f), mSide, ::cRotateAppearX);
    MR::normalize(&mUp);
    MR::setStageBGMState(::cBgmAppearState, ::cBgmAppearFrames);
    setNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvWait));
}

bool SphereSelectorHandle::isPointing() const {
    return MR::isStarPointerInScreen(PLAYER_1);
}

bool SphereSelectorHandle::isHolding() const {
    return isNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvHold));
}

void SphereSelectorHandle::validateRotate() {
    setNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvWait));
}

void SphereSelectorHandle::invalidateRotate() {
    setNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvInvalidRotate));
}

void SphereSelectorHandle::control() {
    rotateAxisY();
    rotateAxisX();
    updateBaseMtx();
    changeBgmRotateState();
    playRotateSE();
    if (isPointing() || MR::isDemoActive()) {
        mNotPointingStep = 0;
    } else {
        mNotPointingStep++;
    }
}

bool SphereSelectorHandle::isWaitOrSpinOrDemoRotate() {
    return (isNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvWait)) ||
            isNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvSpin)) ||
            isNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvDemoRotate)));
}

bool SphereSelectorHandle::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (SphereSelectorFunction::isMsgSelectStart(msg)) {
        appear();
        return true;
    }

    if (SphereSelectorFunction::isMsgSelectEnd(msg)) {
        setNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvDisappear));
        return true;
    }

    if (SphereSelectorFunction::isMsgConfirmStart(msg)) {
        if (mIsFileSelectMode) {
            setNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvIdleForFileSelect));
        } else {
            setNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvGalaxyConfirmStart));
        }

        return true;
    }

    if (SphereSelectorFunction::isMsgConfirmCancel(msg)) {
        if (mIsFileSelectMode) {
            setNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvIdleEndForFileSelect));
        } else {
            setNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvGalaxyConfirmCancel));
        }

        return true;
    }

    if (SphereSelectorFunction::isMsgTargetSelected(msg)) {
        if (isWaitOrSpinOrDemoRotate()) {
            setNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvHold));
            return true;
        }
    }

    return false;
}

bool SphereSelectorHandle::tryRelease() {
    if (!SphereSelectorFunction::isPadButton()) {
        setNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvSpin));
        return true;
    }

    return false;
}

void SphereSelectorHandle::clearPointerVelocity() {
    for (int i = 0; i < ARRAY_SIZE(mPointerVelocities); i++) {
        mPointerVelocities[i].zero();
    }
}

void SphereSelectorHandle::stackPointerVelocity() {
    for (u32 i = 1; i < ARRAY_SIZE(mPointerVelocities); i++) {
        mPointerVelocities[i].set(mPointerVelocities[i - 1]);
    }

    mPointerVelocities[0].set(*MR::getStarPointerScreenVelocity(PLAYER_1));
}

Vec2f* SphereSelectorHandle::getPointerVelocity() {
    u32 max = 0;
    for (u32 i = 1; i < ARRAY_SIZE(mPointerVelocities); i++) {
        if (mPointerVelocities[max].length() < mPointerVelocities[i].length()) {
            max = i;
        }
    }

    return &mPointerVelocities[max];
}

void SphereSelectorHandle::resetRotateParam() {
    mRotateSpeed = 0.0f;
    mTargetRotateSpeed = 0.0f;
    mRotateSpeedX = 0.0f;
    mPrevRotateSpeedX = 0.0f;
}

void SphereSelectorHandle::rotateAxisY() {
    MR::clampBoth(&mRotateSpeed, mTargetRotateSpeed - ::cRotateSpeedChangeMax, mTargetRotateSpeed + ::cRotateSpeedChangeMax);
    MR::clampBoth(&mRotateSpeed, -::cRotateSpeedMaxY, ::cRotateSpeedMaxY);
    mTargetRotateSpeed = mRotateSpeed;
    mRotation.y = MR::repeatDegree(mRotation.y + mRotateSpeed);
}

void SphereSelectorHandle::rotateAxisX() {
    MR::clampBoth(&mRotateSpeedX, mPrevRotateSpeedX - ::cRotateSpeedChangeMax, mPrevRotateSpeedX + ::cRotateSpeedChangeMax);
    MR::clampBoth(&mRotateSpeedX, -::cRotateSpeedMaxX, ::cRotateSpeedMaxX);
    mPrevRotateSpeedX = mRotateSpeedX;
    MR::rotateVecDegree(&mUp, mSide, mRotateSpeedX);
    MR::normalize(&mUp);

    Vec3f rotateMinUp;
    MR::rotateVecDegree(&rotateMinUp, Vec3f(0.0f, 1.0f, 0.0f), mSide, ::cRotateMinX);
    Vec3f rotateMaxUp;
    MR::rotateVecDegree(&rotateMaxUp, Vec3f(0.0f, 1.0f, 0.0f), mSide, ::cRotateMaxX);

    if (mUp.dot(mFront) < rotateMinUp.dot(mFront)) {
        mUp.set(rotateMinUp);
    } else if (rotateMaxUp.dot(mFront) < mUp.dot(mFront)) {
        mUp.set(rotateMaxUp);
    }
}

void SphereSelectorHandle::updateBaseMtx() {
    Pos3f frame;
    MR::makeMtxUpFront(&frame, mUp, mFront);
    frame.zeroTrans();
    Pos3f yawMtx;
    yawMtx.makeRotate(Vec3f(0.0f, 1.0f, 0.0f), MR::toRadian(mRotation.y));
    mBaseMtx.concat(frame, yawMtx);
    mBaseMtx.setTrans(mPosition);
}

void SphereSelectorHandle::changeBgmRotateState() {
    if (MR::abs(mRotateSpeedX) > ::cDemoRotateSpeedY || MR::abs(mRotateSpeed) > ::cDemoRotateSpeedY) {
        if (!mIsRotateBgm) {
            MR::setStageBGMState(::cBgmRotateState, ::cBgmRotateFrames);
        }

        mIsRotateBgm = true;
        return;
    }

    if (mIsRotateBgm) {
        MR::setStageBGMState(::cBgmNotRotateState, ::cBgmNotRotateFrames);
    }

    mIsRotateBgm = false;
}

void SphereSelectorHandle::playRotateSE() {
    if (!(MR::abs(mRotateSpeedX) > ::cDemoRotateSpeedY) && !(MR::abs(mRotateSpeed) > ::cDemoRotateSpeedY)) {
        return;
    }

    f32 verticalRate = MR::abs(mRotateSpeedX) / cNum_playRotateSE_f1;
    f32 horizontalRate = MR::abs(mRotateSpeed) / cNum_playRotateSE_f2;
    f32 rate = verticalRate > horizontalRate ? verticalRate : horizontalRate;

    if (rate > 1.0f) {
        rate = 1.0f;
    }

    MR::startAtmosphereLevelSE("SE_AT_LV_ASTRO_DOME_WIND_1", static_cast< s32 >(cNum_playRotateSE_f3 * rate));
    if (!(MR::abs(mRotateSpeed) >= cNum_playRotateSE_f4) && !(MR::abs(mRotateSpeedX) >= cNum_playRotateSE_f5)) {
        return;
    }

    MR::startAtmosphereLevelSE("SE_AT_LV_ASTRO_DOME_WIND_2");
}

void SphereSelectorHandle::setStateConfirmStartAtFirstStep() {
    if (MR::isFirstStep(this)) {
        resetRotateParam();
        MR::setStageBGMState(::cBgmConfirmState, ::cBgmConfirmFrames);
    }
}

void SphereSelectorHandle::exeWait() {
    if (MR::isFirstStep(this)) {
        resetRotateParam();
    }

    if (isPointing()) {
        SphereSelectorFunction::registerPointingTarget(this, HandlePointingPriority_Last);
    }

    if (mNotPointingStep > ::cDemoRotateStartFrame) {
        setNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvDemoRotate));
    }
}

void SphereSelectorHandle::exeHold() {
    if (MR::isFirstStep(this)) {
        resetRotateParam();
        clearPointerVelocity();
        MR::startSystemSE("SE_DM_ASTRO_HANDLE_GRAB");
    }

    if (isPointing()) {
        stackPointerVelocity();
        mRotateSpeed = ::cHoldRotateRateY * getPointerVelocity()->x;
        if (mRotateSpeed * mTargetRotateSpeed < 0.0f || MR::abs(mRotateSpeed) < MR::abs(mTargetRotateSpeed)) {
            mRotateSpeed = MR::getLinerValue(::cHoldReduceRate, mRotateSpeed, mTargetRotateSpeed, 1.0f);
        }

        mRotateSpeedX = ::cHoldRotateRateX * getPointerVelocity()->y;
        if (mRotateSpeedX * mPrevRotateSpeedX < 0.0f || MR::abs(mRotateSpeedX) < MR::abs(mPrevRotateSpeedX)) {
            mRotateSpeedX = MR::getLinerValue(::cHoldReduceRate, mRotateSpeedX, mPrevRotateSpeedX, 1.0f);
        }
    } else if (mNotPointingStep > ::cHoldReduceOutScreenFrame) {
        mRotateSpeed *= ::cSpinReduceRate;
        mRotateSpeedX *= ::cSpinReduceRate;
    }

    if (tryRelease()) {
        return;
    }
}

void SphereSelectorHandle::exeSpin() {
    mRotateSpeed *= ::cSpinReduceRate;
    mRotateSpeedX *= ::cSpinReduceRate;
    if (MR::isStarPointerInScreen(PLAYER_1)) {
        SphereSelectorFunction::registerPointingTarget(this, HandlePointingPriority_Last);
    }

    if (MR::isNearZero(mRotateSpeed) && MR::isNearZero(mRotateSpeedX)) {
        setNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvWait));
    }
}

void SphereSelectorHandle::exeDemoRotate() {
    mRotateSpeed = ::cDemoRotateSpeedY;
    if (isPointing()) {
        SphereSelectorFunction::registerPointingTarget(this, HandlePointingPriority_Last);
    }

    if (mNotPointingStep == 0) {
        setNerve(GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvWait));
    }
}

void SphereSelectorHandle::exeDisappear() {
    if (MR::isFirstStep(this)) {
        resetRotateParam();
        MR::setStageBGMState(::cBgmDisappearState, ::cBgmDisappearFrames);
    }

    if (MR::isStep(this, ::cBgmDisappearFrames)) {
        kill();
    }
}

void SphereSelectorHandle::exeGalaxyConfirmStart() {
    s32 confirmStartCancelFrame = SphereSelectorFunction::getConfirmStartCancelFrame();
    if (MR::isFirstStep(this)) {
        setStateConfirmStartAtFirstStep();
    }

    MR::setNerveAtStep(this, GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvGalaxyConfirmWait), confirmStartCancelFrame);
}

void SphereSelectorHandle::exeGalaxyConfirmCancel() {
    s32 confirmStartCancelFrame = SphereSelectorFunction::getConfirmStartCancelFrame();
    if (MR::isFirstStep(this)) {
        MR::setStageBGMState(::cBgmNotConfirmState, ::cBgmNotConfirmFrames);
    }

    mPosition.zero();
    MR::setNerveAtStep(this, GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvWait), confirmStartCancelFrame);
}

void SphereSelectorHandle::exeIdleEndForFileSelect() {
    if (MR::isFirstStep(this)) {
        MR::setStageBGMState(::cBgmNotConfirmState, ::cBgmNotConfirmFrames);
    }

    MR::setNerveAtStep(this, GET_NERVE(SphereSelectorHandle, SphereSelectorHandleNrvWait), SphereSelectorFunction::getConfirmStartCancelFrame());
}

void SphereSelectorHandle::exeIdleForFileSelect() {
    setStateConfirmStartAtFirstStep();
}

void SphereSelectorHandle::exeInvalidRotate() {
    if (MR::isFirstStep(this)) {
        resetRotateParam();
    }
}

void SphereSelectorHandle::exeGalaxyConfirmWait() {
    if (MR::isFirstStep(this)) {
        mPosition.zero();
    }
}
