#include "Game/Ride/Pole.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindTurn_f1, "exeBindTurn.f1", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindTurn_f2, "exeBindTurn.f2", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindTurn_f3, "exeBindTurn.f3", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindTurn_f4, "exeBindTurn.f4", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindTurn_f5, "exeBindTurn.f5", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindTurn_f6, "exeBindTurn.f6", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeBindHandstandTurn_f1, "exeBindHandstandTurn.f1", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f1, "receiveOtherMsg.f1", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f2, "receiveOtherMsg.f2", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f3, "receiveOtherMsg.f3", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f4, "receiveOtherMsg.f4", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f5, "receiveOtherMsg.f5", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryJump_f1, "tryJump.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryJump_f2, "tryJump.f2", 360.0f);

namespace {
    NEBULA_TUNABLE(f32, sSensorOffsetY, 50.0f);
    NEBULA_TUNABLE(f32, sPlayerDistanceToValid, 300.0f);
    NEBULA_TUNABLE(f32, sPlayerSpeedToCatchMin, 6.0f);
    NEBULA_TUNABLE(f32, sPlayerSpeedToCatchMax, 10.0f);
    NEBULA_TUNABLE(f32, sBindStartRotateSpeed, 5.0f);
    NEBULA_TUNABLE(f32, sBindStartRotateSpeedFast, 9.0f);
    NEBULA_TUNABLE(f32, sInvalidPlayerSpeedMaxXZ, 5.0f);
    NEBULA_TUNABLE(f32, sInvalidPlayerSpeedMinY, 1.0f);
    NEBULA_TUNABLE(s32, sStepInvalid, 45);
    NEBULA_TUNABLE(f32, sClimbUpSpeedRate, 6.0f);
    NEBULA_TUNABLE(f32, sFallDownAccelRate, 0.3f);
    NEBULA_TUNABLE(f32, sFallDownSpeedMaxY, 15.0f);
    NEBULA_TUNABLE(f32, sSquareFallDownAccelRate, 0.5f);
    NEBULA_TUNABLE(f32, sSquareFallDownSpeedMaxY, 18.0f);
    NEBULA_TUNABLE(f32, sTurnStickMin, 0.8f);
    NEBULA_TUNABLE(f32, sRotateSpeedRate, 2.5f);
    NEBULA_TUNABLE(f32, sClimbUpRotateSpeedRate, 0.0f);
    NEBULA_TUNABLE(f32, sFallDownRotateSpeedRate, 0.8f);
    NEBULA_TUNABLE(f32, sJumpSpeedXZ, 10.0f);
    NEBULA_TUNABLE(f32, sJumpSpeedY, 25.0f);
    NEBULA_TUNABLE(s32, sStepJumpReverseHandstandStart, 25);
    NEBULA_TUNABLE(s32, sStepJumpReverseHandstandEnd, 25);
    NEBULA_TUNABLE(f32, sSquareJumpPosOffset, 50.0f);
    NEBULA_TUNABLE(s32, sStepSquareJumpChangeDir, 7);
    NEBULA_TUNABLE(f32, sEndHandstandStickY, -0.8f);
    NEBULA_TUNABLE(f32, sLandingHeightMin, 120.0f);
    NEBULA_TUNABLE(f32, sLandingHeightMinSquare, 50.0f);
    NEBULA_TUNABLE(f32, sPlayerPosOffsetAtLanding, 30.0f);
    NEBULA_TUNABLE(f32, sClimbUpAnimFrameRate, 1.75f);
    NEBULA_TUNABLE(f32, sSquareClimbUpAnimFrameRate, 1.6f);
    static const s32 sStepDemoAppear = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, sHeightRate, 100.0f);
    NEBULA_TUNABLE(f32, sSquareHeightOffset, 100.0f);
};  // namespace

namespace NrvPole {
    NEW_NERVE(PoleNrvDemoAppear, Pole, DemoAppear);
    NEW_NERVE(PoleNrvFree, Pole, Free);
    NEW_NERVE(PoleNrvFreeInvalid, Pole, FreeInvalid);
    NEW_NERVE(PoleNrvBindStart, Pole, BindStart);
    NEW_NERVE(PoleNrvBindStartFast, Pole, BindStart);
    NEW_NERVE(PoleNrvBindWait, Pole, BindWait);
    NEW_NERVE(PoleNrvBindTurnLeft, Pole, BindTurn);
    NEW_NERVE(PoleNrvBindTurnRight, Pole, BindTurn);
    NEW_NERVE(PoleNrvBindTurnEnd, Pole, BindWait);
    NEW_NERVE(PoleNrvBindClimbUp, Pole, BindClimbUp);
    NEW_NERVE(PoleNrvBindFallDown, Pole, BindFallDown);
    NEW_NERVE(PoleNrvBindHandstandStart, Pole, BindHandstandStart);
    NEW_NERVE(PoleNrvBindHandstandWait, Pole, BindHandstandWait);
    NEW_NERVE(PoleNrvBindHandstandEnd, Pole, BindHandstandEnd);
    NEW_NERVE(PoleNrvBindHandstandTurn, Pole, BindHandstandTurn);
};  // namespace NrvPole

Pole::Pole(const char* pName)
    : LiveActor(pName), mBasePos(0.0f, 0.0f, 0.0f), mTopPos(0.0f, 0.0f, 0.0f), mHeight(), mDisableHandstand(), mIsSquare(), mNoModel(),
      mInvertStick(), mUsePole9mCollision(), mClimbCoord(), mClimbSpeed(), mRider(), mCameraInfo(), mCenterPos(0.0f, 0.0f, 0.0f),
      mSide(1.0f, 0.0f, 0.0f), mUp(0.0f, 1.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f), mTopMtx(), mBottomMtx() {
    lumacore::setClass(this, lumacore::kClassId< Pole >);
    mPosMtx.identity();
    mBaseMtx.identity();
}

void Pole::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mBasePos.set(mPosition);
    mHeight = ::sHeightRate * mScale.y;
    mScale.set(1.0f, 1.0f, 1.0f);

    MR::getPlacementArg0NoInit(rIter, &mDisableHandstand);
    MR::getPlacementArg1NoInit(rIter, &mInvertStick);
    s32 use9mCollision = -1;
    MR::getPlacementArg2NoInit(rIter, &use9mCollision);
    mUsePole9mCollision = use9mCollision == 1;

    const char* name = nullptr;
    MR::getObjectName(&name, rIter);
    mNoModel = MR::isEqualSubString(name, "NoModel");
    if (MR::isEqualSubString(name, "Square")) {
        mIsSquare = true;
        mDisableHandstand = true;
    }

    Pos3f mtx;
    MR::makeMtxTR(mtx.toMtxPtr(), this);

    mtx.getXDir(mSide);
    mtx.getYDir(mUp);
    mtx.getZDir(mFront);

    mPosMtx.set(mtx);
    mPosMtx.invert(mPosMtx);
    mPosMtx.zeroTrans();

    initHitSensor(3);
    MR::addHitSensorBinder(this, "bind", 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorRide(this, "ride", 8, cNum_init_f2, Vec3f(0.0f, ::sSensorOffsetY, 0.0f));
    MR::addHitSensorPush(this, "push", 8, cNum_init_f3, Vec3f(0.0f, 0.0f, 0.0f));

    if (!mNoModel) {
        initModelManagerWithAnm(name, nullptr, false);
        if (mUsePole9mCollision) {
            MR::initCollisionParts(this, "Pole9m", getSensor("ride"), nullptr);
        } else {
            MR::initCollisionParts(this, name, getSensor("ride"), nullptr);
        }
    } else {
        mBaseMtx.set(mtx);
        if (MR::isEqualString(name, "PoleNoModel")) {
            ResourceHolder* resource1 = MR::createAndAddResourceHolder("Pole");
            MR::initCollisionPartsFromResourceHolder(this, "Pole", getSensor("ride"), resource1, mBaseMtx.toMtxPtr());
        } else {
            ResourceHolder* resource2 = MR::createAndAddResourceHolder("PoleSquare");
            MR::initCollisionPartsFromResourceHolder(this, "PoleSquare", getSensor("ride"), resource2, mBaseMtx.toMtxPtr());
        }
    }

    if (MR::isEqualString(name, "Pole") || MR::isEqualString(name, "PoleSquare")) {
        mTopMtx = MR::getJointMtx(this, "PoleTop");
        mBottomMtx = MR::getJointMtx(this, "PoleBottom");
        if (MR::isEqualString(name, "Pole") && !mDisableHandstand) {
            MR::hideMaterial(this, "PoleTopStopMat_v");
        }
        updateTopPos(mHeight);
    } else {
        if (MR::isEqualString(name, "TreeCube")) {
            mHeight = cNum_init_f4;
        }

        mTopPos.set(mUp);
        mTopPos.scale(mHeight);
        mTopPos.add(mBasePos);

        if (!mNoModel) {
            mBottomMtx = MR::getJointMtx(this, "world_root");
        }
    }

    if (!mNoModel) {
        MR::setMtxAxisXYZ(mBottomMtx, mSide, mUp, mFront);
        MR::setMtxTrans(mBottomMtx, mBasePos);
    }

    if (mNoModel) {
        MR::connectToSceneMapObjMovement(this);
    } else {
        MR::connectToSceneMapObj(this);
    }

    initSound(4, false);
    MR::initActorCamera(this, rIter, &mCameraInfo);
    initNerve(GET_NERVE(Pole, PoleNrvFree));

    mCenterPos.set(mUp);
    mCenterPos.scale(mHeight / cNum_init_f5);
    mCenterPos.add(mBasePos);
    MR::setClippingTypeSphere(this, mHeight / cNum_init_f6, &mCenterPos);
    MR::useStageSwitchSleep(this, rIter);

    if (MR::tryRegisterDemoCast(this, rIter)) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void Pole::appear() {
    LiveActor::appear();
    setNerve(GET_NERVE(Pole, PoleNrvDemoAppear));
}

void Pole::calcAnim() {
    if (!mNoModel) {
        MR::updateMaterial(this);
    }
}

void Pole::exeDemoAppear() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);
        MR::invalidateCollisionParts(this);
    }

    updateTopPos(mHeight * MR::calcNerveRate(this, ::sStepDemoAppear));

    if (MR::isDemoActive()) {
        MR::startLevelSound(this, "SE_OJ_LV_POLE_APPEAR");
    }

    if (MR::isStep(this, ::sStepDemoAppear)) {
        updateTopPos(mHeight);
        MR::validateCollisionParts(this);
        MR::validateHitSensors(this);
        setNerve(GET_NERVE(Pole, PoleNrvFree));
    }
}

void Pole::exeFree() {
    MR::calcPerpendicFootToLineInside(&mPosition, *MR::getPlayerCenterPos(), mTopPos, mBasePos);
}

void Pole::exeFreeInvalid() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);
    }

    MR::calcPerpendicFootToLineInside(&mPosition, *MR::getPlayerCenterPos(), mTopPos, mBasePos);

    if (MR::isOnGroundPlayer() || MR::calcDistanceToPlayer(this) > ::sPlayerDistanceToValid || MR::isGreaterStep(this, ::sStepInvalid)) {
        MR::validateHitSensors(this);
        setNerve(GET_NERVE(Pole, PoleNrvFree));
    }
}

void Pole::exeBindStart() {
    if (MR::isFirstStep(this)) {
        if (mIsSquare) {
            MR::startBckPlayer("SquarePoleStart");
        } else {
            MR::startBckPlayer("TreeClimbStart");
        }
    }

    if (!mIsSquare) {
        f32 rotateSpeed = ::sBindStartRotateSpeed;
        if (isNerve(GET_NERVE(Pole, PoleNrvBindStartFast))) {
            rotateSpeed = ::sBindStartRotateSpeedFast;
        }

        s32 frame = MR::getBckCtrl(mRider)->getEnd();
        if (frame > 0) {
            mRotation.y += rotateSpeed * MR::getEaseOutValue(1.0f - ((f32)getNerveStep() / frame), 0.0f, 1.0f, 1.0f);
        }
    }

    if (!tryJump(false, 0.0f) && MR::isBckStopped(mRider)) {
        setNerve(GET_NERVE(Pole, PoleNrvBindWait));
    }
}

void Pole::exeBindWait() {
    if (MR::isFirstStep(this)) {
        if (mIsSquare) {
            if (!MR::isBckPlaying(mRider, "SquarePoleWait")) {
                MR::startBckPlayer("SquarePoleWait");
            }
        } else {
            MR::startBckPlayer("TreeWait");
        }
        mClimbSpeed = 0.0f;
    }

    if (tryJump(false, 0.0f)) {
        return;
    }

    if (!isNerve(GET_NERVE(Pole, PoleNrvBindTurnEnd))) {
        if (tryTurn()) {
            return;
        }
    } else if (!isEnableTurn()) {
        setNerve(GET_NERVE(Pole, PoleNrvBindWait));
        return;
    }

    if (!tryClimbUp()) {
        if (tryFallDown()) {
            return;
        }
    }
}

void Pole::exeBindTurn() {
    if (MR::isFirstStep(this)) {
        if (mIsSquare) {
            if (isNerve(GET_NERVE(Pole, PoleNrvBindTurnLeft))) {
                MR::startBckPlayer("SquarePoleTurnL");
            } else {
                MR::startBckPlayer("SquarePoleTurnR");
            }
        } else {
            MR::startBckPlayer("ClimbTurn");
        }
    }

    if (!mIsSquare) {
        mRotation.y += ::sRotateSpeedRate * getPoleMoveStickX();
        mRotation.y = MR::repeat(mRotation.y, 0.0f, cNum_exeBindTurn_f1);
    }

    f32 jump = 0.0f;

    if (mIsSquare && MR::isGreaterStep(this, ::sStepSquareJumpChangeDir)) {
        if (isNerve(GET_NERVE(Pole, PoleNrvBindTurnLeft))) {
            jump = -cNum_exeBindTurn_f2;
        } else {
            jump = cNum_exeBindTurn_f3;
        }
    }

    if (tryJump(false, jump)) {
        return;
    }

    if (mIsSquare) {
        if (MR::isBckStopped(mRider)) {
            if (isNerve(GET_NERVE(Pole, PoleNrvBindTurnRight))) {
                mRotation.y += cNum_exeBindTurn_f4;
            } else {
                mRotation.y -= cNum_exeBindTurn_f5;
            }

            mRotation.y = MR::repeat(mRotation.y, 0.0f, cNum_exeBindTurn_f6);

            MR::startBckPlayer("SquarePoleWait", "SquarePoleTurnEnd");
            setNerve(GET_NERVE(Pole, PoleNrvBindTurnEnd));
        }
    } else if (!isEnableTurn()) {
        setNerve(GET_NERVE(Pole, PoleNrvBindWait));
    }
}

void Pole::exeBindClimbUp() {
    if (MR::isFirstStep(this)) {
        if (mIsSquare) {
            MR::startBckPlayer("SquarePoleClimb");
            MR::setBckRate(mRider, ::sSquareClimbUpAnimFrameRate);
        } else {
            MR::startBckPlayer("TreeClimb");
            MR::setBckRate(mRider, ::sClimbUpAnimFrameRate);
        }
        mClimbSpeed = 0.0f;
    }

    if (!tryJump(false, ::sClimbUpRotateSpeedRate) && !tryTurn() && !tryFallDown()) {
        if (0.0f == getPoleMoveStickY()) {
            setNerve(GET_NERVE(Pole, PoleNrvBindWait));
            return;
        }

        mClimbCoord += ::sClimbUpSpeedRate;
        if (mClimbCoord > mHeight) {
            mClimbCoord = mHeight;

            if (!mDisableHandstand) {
                updateBindTrans();
                setNerve(GET_NERVE(Pole, PoleNrvBindHandstandStart));
                return;
            }

            if (mIsSquare) {
                if (!MR::isBckPlaying(mRider, "SquarePolePushWait")) {
                    MR::startBckPlayer("SquarePolePushWait");
                }
            }
        } else {
            MR::tryRumblePadVeryWeak(this, PLAYER_1);
        }
        updateBindTrans();
    }
}

void Pole::exeBindFallDown() {
    if (MR::isFirstStep(this)) {
        if (mIsSquare) {
            MR::startBckPlayer("SquarePoleFall");
        } else {
            MR::startBckPlayer("TreeFall");
        }
    }

    if (mIsSquare) {
        mClimbSpeed -= ::sSquareFallDownAccelRate;
        mClimbSpeed = mClimbSpeed >= -::sSquareFallDownSpeedMaxY ? mClimbSpeed : -::sSquareFallDownSpeedMaxY;
    } else {
        mClimbSpeed -= ::sFallDownAccelRate;
        mClimbSpeed = mClimbSpeed >= -::sFallDownSpeedMaxY ? mClimbSpeed : -::sFallDownSpeedMaxY;
    }

    mClimbCoord += mClimbSpeed;

    if (!mIsSquare) {
        mRotation.y += ::sFallDownRotateSpeedRate * mClimbSpeed;
    }

    updateBindTrans();
    MR::tryRumblePadVeryWeak(this, PLAYER_1);
    MR::startLevelSound(mRider, "SE_PM_LV_POLE_SLIDE");

    if ((!mIsSquare && mClimbCoord <= ::sLandingHeightMin) || (mIsSquare && mClimbCoord <= ::sLandingHeightMinSquare)) {
        Pos3f mtx;
        calcGravityMtx(&mtx);
        Vec3f front(0.0f, 0.0f, 0.0f);
        front.x = Angle::sinDegree(mRotation.y);
        front.z = Angle::cosDegree(mRotation.y);
        mtx.mult(front, front);

        Vec3f pos = front * -::sPlayerPosOffsetAtLanding;
        pos.add(mBasePos);

        MR::setPlayerPos(pos);
        MR::setPlayerFrontTargetVec(front, 1);
        MR::endActorCamera(this, mCameraInfo, true);
        MR::endBindAndPlayerWait(this);

        mRider = nullptr;
        setNerve(GET_NERVE(Pole, PoleNrvFreeInvalid));
        return;
    }

    if (!tryJump(false, 0.0f) && !tryTurn() && !tryClimbUp()) {
        if (0.0f == getPoleMoveStickY()) {
            setNerve(GET_NERVE(Pole, PoleNrvBindWait));
        }
    }
}

void Pole::exeBindHandstandStart() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("TreeHandstandStart");
        MR::startSound(mRider, "SE_PM_POLE_STAND");
        MR::startSound(mRider, "SE_PV_LIFT_UP");
    }

    if (tryJump(!MR::isLessStep(this, ::sStepJumpReverseHandstandStart), 0.0f)) {
        return;
    }

    if (!tryHandstandTurn()) {
        if (MR::isBckStopped(mRider)) {
            setNerve(GET_NERVE(Pole, PoleNrvBindHandstandWait));
        }
    }
}

void Pole::exeBindHandstandWait() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("TreeHandstandWait");
    }

    if (!tryJump(true, 0.0f) && !tryHandstandTurn()) {
        if (getPoleMoveStickY() < ::sEndHandstandStickY) {
            setNerve(GET_NERVE(Pole, PoleNrvBindHandstandEnd));
        }
    }
}

void Pole::exeBindHandstandEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("TreeHandstandEnd");
    }

    if (tryJump(MR::isLessStep(this, ::sStepJumpReverseHandstandEnd), 0.0f)) {
        return;
    }

    if (!tryTurn()) {
        if (MR::isBckStopped(mRider)) {
            setNerve(GET_NERVE(Pole, PoleNrvBindWait));
        }
    }
}

void Pole::exeBindHandstandTurn() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("TreeHandstandTurn");
    }

    mRotation.y += getPoleMoveStickX() * ::sRotateSpeedRate;
    mRotation.y = MR::repeat(mRotation.y, 0.0f, cNum_exeBindHandstandTurn_f1);

    if (!tryJump(true, 0.0f) && !isEnableTurn()) {
        setNerve(GET_NERVE(Pole, PoleNrvBindHandstandWait));
    }
}

void Pole::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mRider != nullptr && MR::isSensorRide(pSender)) {
        MR::tryGetItem(pSender, pReceiver);
        return;
    }

    if (!MR::isSensorPush(pSender) || isNerve(GET_NERVE(Pole, PoleNrvFreeInvalid))) {
        return;
    }

    if (MR::isOnGroundPlayer()) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool Pole::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (mRider != nullptr && MR::isSensor(pReceiver, "bind")) {
        MR::endActorCamera(this, mCameraInfo, true);
        MR::endBindAndPlayerDamageMsg(this, msg);

        mRider = nullptr;
        setNerve(GET_NERVE(Pole, PoleNrvFreeInvalid));
        return true;
    }
    return false;
}

bool Pole::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgIsRushTakeOver(msg)) {
        return true;
    }

    if (MR::isMsgAutoRushBegin(msg)) {
        if (MR::isOnGroundPlayer() || isNerve(GET_NERVE(Pole, PoleNrvFreeInvalid))) {
            return false;
        }

        Vec3f velXZ = *MR::getPlayerVelocity();
        MR::vecKillElement(velXZ, mUp, &velXZ);
        f32 speedXZ = velXZ.length();
        f32 speedY = mUp.dot(*MR::getPlayerVelocity());
        if (MR::abs(speedXZ) < ::sInvalidPlayerSpeedMaxXZ && speedY > ::sInvalidPlayerSpeedMinY) {
            return false;
        }

        Vec3f front = mPosition;
        front.sub(*MR::getPlayerPos());
        MR::vecKillElement(front, mUp, &front);
        if (MR::isNearZero(front)) {
            front.set(mFront);
        } else {
            MR::normalize(&front);
        }

        mPosMtx.mult(front, front);
        front.y = 0.0f;

        if (!MR::isNearZero(front)) {
            MR::normalize(&front);
        } else {
            front.set(mFront);
        }
        mRider = pSender->mHost;
        mRotation.y = MR::calcRotateY(front.x, front.z);

        if (mIsSquare) {
            s32 side = (cNum_receiveOtherMsg_f1 + mRotation.y) / cNum_receiveOtherMsg_f2;
            mRotation.y = cNum_receiveOtherMsg_f3 * side;
        }

        Vec3f climbProj = mPosition;
        climbProj.sub(mBasePos);
        mClimbCoord = climbProj.dot(mUp);
        if (mClimbCoord < cNum_receiveOtherMsg_f4) {
            mClimbCoord = cNum_receiveOtherMsg_f5;
        } else if (mClimbCoord > mHeight) {
            mClimbCoord = mHeight;
        }
        mClimbSpeed = 0.0f;
        updateBindTrans();

        MR::invalidateClipping(this);
        MR::startSound(mRider, "SE_PM_GRAB_OBJ");
        MR::startSound(mRider, "SE_PV_LIFT_UP");
        MR::startActorCameraNoTarget(this, mCameraInfo);
        MR::tryRumblePadWeak(this, PLAYER_1);

        if (speedXZ > ::sPlayerSpeedToCatchMax) {
            setNerve(GET_NERVE(Pole, PoleNrvBindStartFast));
        } else if (speedXZ > ::sPlayerSpeedToCatchMin) {
            setNerve(GET_NERVE(Pole, PoleNrvBindStart));
        } else {
            setNerve(GET_NERVE(Pole, PoleNrvBindWait));
        }

        return true;
    }

    if (MR::isMsgUpdateBaseMtx(msg)) {
        Pos3f posMtx;
        Rot3f rotMtx;
        posMtx.identity();
        posMtx.setXYZDir(mSide, mUp, mFront);

        rotMtx.identity();
        MR::makeMtxTransRotateY(rotMtx, this);
        MR::multMtx(posMtx, rotMtx, posMtx);
        posMtx.setTrans(mPosition);
        MR::setBaseTRMtx(mRider, posMtx);
        return true;
    }

    if (MR::isMsgRushCancel(msg)) {
        if (!MR::isDemoActive()) {
            MR::startSound(mRider, "SE_PM_JUMP_M");
            MR::startSound(mRider, "SE_PV_JUMP_JOY");
        }
        mRider = nullptr;
        setNerve(GET_NERVE(Pole, PoleNrvFree));
        return true;
    }

    return false;
}

bool Pole::tryJump(bool handstand, f32 rotateSpeed) {
    if (MR::isPressedJump(PLAYER_1) || MR::testSystemTriggerA()) {
        Pos3f pos;
        calcGravityMtx(&pos);

        f32 frontAngle = MR::repeat(rotateSpeed + mRotation.y + cNum_tryJump_f1, 0.0f, cNum_tryJump_f2);
        Vec3f jumpFront(0.0f, 0.0f, 0.0f);
        jumpFront.x = Angle::sinDegree(frontAngle);
        jumpFront.z = Angle::cosDegree(frontAngle);

        if (handstand) {
            jumpFront.scale(-1.0f);
        }

        Vec3f jumpVec;
        jumpVec.x = ::sJumpSpeedXZ * jumpFront.x;
        jumpVec.y = ::sJumpSpeedY;
        jumpVec.z = ::sJumpSpeedXZ * jumpFront.z;
        pos.mult(jumpVec, jumpVec);
        pos.mult(jumpFront, jumpFront);
        MR::setPlayerFrontTargetVec(jumpFront, 1);

        if (handstand) {
            MR::startBckPlayer("TreeHandStandJump");
        } else {
            MR::startBckPlayer("WallJump");
        }

        MR::startSound(mRider, "SE_PM_JUMP_M");
        MR::startSound(mRider, "SE_PV_JUMP_JOY");
        MR::endActorCamera(this, mCameraInfo, 1);

        if (mIsSquare) {
            Vec3f jumpPos = jumpFront * ::sSquareJumpPosOffset;
            jumpPos.add(*MR::getPlayerPos());
            MR::setPlayerPos(jumpPos);
        }

        MR::endBindAndPlayerJump(this, jumpVec, 0);

        if (handstand) {
            MR::becomeContinuousBckPlayer();
        }

        mRider = nullptr;
        setNerve(GET_NERVE(Pole, PoleNrvFreeInvalid));
        return true;
    }
    return false;
}

bool Pole::tryTurn() {
    if (isEnableTurn()) {
        if (getPoleMoveStickX() > 0.0f) {
            setNerve(GET_NERVE(Pole, PoleNrvBindTurnRight));
        } else {
            setNerve(GET_NERVE(Pole, PoleNrvBindTurnLeft));
        }
        return true;
    }
    return false;
}

bool Pole::tryClimbUp() {
    if (getPoleMoveStickY() > 0.0f) {
        setNerve(GET_NERVE(Pole, PoleNrvBindClimbUp));
        return true;
    }
    return false;
}

bool Pole::tryFallDown() {
    if (getPoleMoveStickY() < 0.0f) {
        setNerve(GET_NERVE(Pole, PoleNrvBindFallDown));
        return true;
    }
    return false;
}

bool Pole::tryHandstandTurn() {
    if (isEnableTurn()) {
        setNerve(GET_NERVE(Pole, PoleNrvBindHandstandTurn));
        return true;
    }
    return false;
}

bool Pole::isEnableTurn() const {
    return MR::abs(getPoleMoveStickX()) > ::sTurnStickMin;
}

void Pole::updateBindTrans() {
    mPosition.set(mUp);
    mPosition.scale(mClimbCoord);
    mPosition.add(mBasePos);
}

void Pole::calcGravityMtx(Pos3f* pos) {
    pos->identity();
    MR::calcGravity(this);
    Vec3f up(-mGravity.x, -mGravity.y, -mGravity.z);
    MR::makeMtxUpFront(pos, up, mFront);
}

void Pole::updateTopPos(f32 top) {
    f32 topCoord = top;

    if (mIsSquare) {
        topCoord += ::sSquareHeightOffset;
    }

    mTopPos.set(mUp);
    mTopPos.scale(topCoord);
    mTopPos.add(mBasePos);
    MR::setMtxAxisXYZ(mTopMtx, mSide, mUp, mFront);
    MR::setMtxTrans(mTopMtx, mTopPos);
}

f32 Pole::getPoleMoveStickX() const {
    if (mInvertStick) {
        return -MR::getMoveStickX(PLAYER_1);
    } else {
        return MR::getMoveStickX(PLAYER_1);
    }
}

f32 Pole::getPoleMoveStickY() const {
    if (mInvertStick) {
        return -MR::getMoveStickY(PLAYER_1);
    } else {
        return MR::getMoveStickY(PLAYER_1);
    }
}
