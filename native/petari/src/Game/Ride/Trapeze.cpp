#include "Game/Ride/Trapeze.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Ride/SwingRopePoint.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 130.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f2, "draw.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f1, "receiveOtherMsg.f1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f2, "receiveOtherMsg.f2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryJump_f1, "tryJump.f1", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHangPoint_f1, "updateHangPoint.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHangPoint_f2, "updateHangPoint.f2", 100.0f);

namespace {
    NEBULA_TUNABLE(f32, sStickLength, 120.0f);
    NEBULA_TUNABLE(f32, sDrawWidthX, 12.0f);
    NEBULA_TUNABLE(f32, sDrawWidthZ, 12.0f);
    NEBULA_TUNABLE(f32, sDrawWidthLongOffsetX, 7.0f);
    NEBULA_TUNABLE(f32, sDrawWidthShortOffsetX, 0.0f);
    NEBULA_TUNABLE(f32, sDrawWidthShortOffsetZ, 7.0f);
    NEBULA_TUNABLE(f32, sTexRate, 0.003f);
    NEBULA_TUNABLE(f32, sTexRateDiff1, 0.7f);
    NEBULA_TUNABLE(f32, sTexRateDiff2, 0.5f);
    NEBULA_TUNABLE(f32, sHangAccelRate, 5.0f);
    NEBULA_TUNABLE(f32, sHangPointGravity, 0.8f);
    NEBULA_TUNABLE(f32, sHangPointFrictionRate, 0.995f);
    NEBULA_TUNABLE(f32, sHangCoordSpeed, 0.5f);
    NEBULA_TUNABLE(f32, sSensorOffsetY, -70.0f);
    NEBULA_TUNABLE(s32, sAnimInterpoleFrame, 12);
    NEBULA_TUNABLE(f32, sSpeedToSwingWait, 10.0f);
    NEBULA_TUNABLE(f32, sAxisToSwingWait, 0.99f);
    NEBULA_TUNABLE(f32, sJumpSpeedRate, 1.0f);
    NEBULA_TUNABLE(f32, sJumpSpeedMinXZ, 15.0f);
    NEBULA_TUNABLE(f32, sJumpSpeedMaxXZ, 20.0f);
    NEBULA_TUNABLE(f32, sJumpSpeedMinY, 20.0f);
    NEBULA_TUNABLE(f32, sJumpSpeedMaxY, 50.0f);
    NEBULA_TUNABLE(f32, sStickPointGravity, 1.0f);
    NEBULA_TUNABLE(f32, sStickPointFrictionRate, 0.995f);
    NEBULA_TUNABLE(f32, sPointSpeedMin, 1.0f);
    NEBULA_TUNABLE(f32, sPointNormalMin, 0.001f);
    NEBULA_TUNABLE(f32, sDistanceToValid, 500.0f);
    NEBULA_TUNABLE(s32, sStepToValid, 40);
    NEBULA_TUNABLE(f32, sDistanceStartBindZ, 80.0f);
    NEBULA_TUNABLE(s32, sStepSlideToRumble, 20);
    NEBULA_TUNABLE(f32, sSpeedMinToKeepFreeSwing, 80.0f);
    NEBULA_TUNABLE(f32, sTransDiffMinY, 1.0f);
    NEBULA_TUNABLE(f32, sTexRateDiff0, 0.0f);

    static Color8 sColorPlusZ(0xFF, 0xFF, 0xFF, 0xFF);
    static Color8 sColorPlusX(0xB4, 0xB4, 0xB4, 0xFF);
    static Color8 sColorMinusX(0x64, 0x64, 0x64, 0xFF);
};  // namespace

Trapeze::Trapeze(const char* pName)
    : LiveActor(pName), mSide(1.0f, 0.0f, 0.0f), mUp(0.0f, 1.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f), mRopeLength(), mStickPoint(), mTrapezeModel(),
      mRider(), mHangPoint(), mGrabCoord(), mSwingVel(), mSwingReverse(), mIsSwingFront(), mIsSwingBack(), mWasSwingFront(), mWasSwingBack(),
      mCameraInfo(), mPrevSoundLvl() {
    mPosMtx.identity();
}

void Trapeze::init(const TableIter& rIter) {
    MR::createSceneObj(SceneObj_TrapezeRopeDrawInit);
    MR::connectToScene(this, MR::MovementType_Ride, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_Trapeze);
    MR::initDefaultPos(this, rIter);

    Pos3f mtx;
    MR::makeMtxTR(mtx, this);

    mtx.getXDir(mSide);
    mtx.getYDir(mUp);
    mtx.getZDir(mFront);

    mRopeLength = mScale.y * cNum_init_f1;
    mScale.set(1.0f, 1.0f, 1.0f);

    Vec3f spawnPos(mPosition.x, mPosition.y - mRopeLength, mPosition.z);
    mStickPoint = new SwingRopePoint(spawnPos);
    mHangPoint = new SwingRopePoint(mPosition);

    mStickPoint->updatePosAndAxis(mFront, ::sStickPointFrictionRate);
    mHangPoint->updatePosAndAxis(mFront, ::sHangPointFrictionRate);

    initNerve(GET_NERVE(Trapeze, TrapezeNrvFree));

    initHitSensor(2);
    MR::addHitSensorCallbackBinder(this, "bind", 8, cNum_init_f2);
    MR::addHitSensorCallbackRide(this, "body", 8, cNum_init_f3);

    MR::calcGravity(this);

    initSound(4, false);

    MR::setClippingTypeSphere(this, mRopeLength);

    mTrapezeModel = new PartsModel(this, "棒", "Trapeze", mPosMtx, MR::DrawBufferType_Enemy, false);
    mTrapezeModel->initWithoutIter();

    MR::initShadowFromCSV(mTrapezeModel, "Shadow");
    MR::setShadowDropLength(mTrapezeModel, nullptr, mRopeLength + cNum_init_f4);
    MR::registerDemoSimpleCastAll(mTrapezeModel);
    MR::initActorCamera(this, rIter, &mCameraInfo);
    makeActorAppeared();
}

void Trapeze::draw() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    TypedDraw site(MR::getSceneObj< TrapezeRopeDrawInit >(SceneObj_TrapezeRopeDrawInit)->mMaterial, nullptr, MR::getCameraViewMtx());

    Vec3f left = mSide * -(::sStickLength / cNum_draw_f1);
    Vec3f right = mSide * (::sStickLength / cNum_draw_f2);

    Vec3f grabLeft = left;
    Vec3f grabRight = right;
    Vec3f swingLeft = left;
    Vec3f swingRight = right;

    left.add(mPosition);
    right.add(mPosition);
    grabLeft.add(mHangPoint->mPosition);
    grabRight.add(mHangPoint->mPosition);
    swingLeft.add(mStickPoint->mPosition);
    swingRight.add(mStickPoint->mPosition);

    // Which ropes draw changes with the rider and the grab; each rope has numbers of its own (three strips each), so a rope
    // is paired with itself from frame to frame and a different rope that draws in its place is not blended from it.
    enum { Rope_WholeLeft, Rope_WholeRight, Rope_HeldLeft, Rope_HeldRight, Rope_SwingLeft, Rope_SwingRight, cStripsPerRope = 3 };
    if (mRider != nullptr) {
        site.setPart(Rope_HeldLeft * cStripsPerRope);
        drawRope(site, left, grabLeft, mHangPoint->mSide, mHangPoint->mFront, 0.0f, mGrabCoord * ::sTexRate);
        site.setPart(Rope_HeldRight * cStripsPerRope);
        drawRope(site, right, grabRight, mHangPoint->mSide, mHangPoint->mFront, 0.0f, mGrabCoord * ::sTexRate);

        if (mGrabCoord < mRopeLength) {
            site.setPart(Rope_SwingLeft * cStripsPerRope);
            drawRope(site, grabLeft, swingLeft, mStickPoint->mSide, mStickPoint->mFront, mGrabCoord * ::sTexRate, mRopeLength * ::sTexRate);
            site.setPart(Rope_SwingRight * cStripsPerRope);
            drawRope(site, grabRight, swingRight, mStickPoint->mSide, mStickPoint->mFront, mGrabCoord * ::sTexRate, mRopeLength * ::sTexRate);
        }
    } else {
        site.setPart(Rope_WholeLeft * cStripsPerRope);
        drawRope(site, left, swingLeft, mStickPoint->mSide, mStickPoint->mFront, 0.0f, mRopeLength * ::sTexRate);
        site.setPart(Rope_WholeRight * cStripsPerRope);
        drawRope(site, right, swingRight, mStickPoint->mSide, mStickPoint->mFront, 0.0f, mRopeLength * ::sTexRate);
    }
}

void Trapeze::exeStop() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        getSensor("body")->invalidate();
    }
}

void Trapeze::exeFree() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
    }

    updateStick(mPosition, mRopeLength);
    updateStickMtx();

    if (isNerve(GET_NERVE(Trapeze, TrapezeNrvFree))) {
        if (mStickPoint->mVelocity.squared() < ::sPointSpeedMin * ::sPointSpeedMin && 1.0f - MR::abs(mStickPoint->mUp.y) < ::sPointNormalMin) {
            setNerve(GET_NERVE(Trapeze, TrapezeNrvStop));
        }
    }
}

void Trapeze::exeFreeInvalid() {
    exeFree();
    Vec3f posDiffHoriz = *MR::getPlayerPos();
    posDiffHoriz.sub(mStickPoint->mPosition);
    posDiffHoriz.y = 0.0f;

    if (MR::isOnGroundPlayer() || MR::isGreaterStep(this, ::sStepToValid) || posDiffHoriz.squared() > ::sDistanceToValid * ::sDistanceToValid) {
        setNerve(GET_NERVE(Trapeze, TrapezeNrvFree));
    }
}

void Trapeze::exeSwingWait() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("TrapezeWait", ::sAnimInterpoleFrame);
    }

    if (updateBind()) {
        return;
    }

    if (mIsSwingFront) {
        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingFrontStart));
    } else if (mIsSwingBack) {
        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingBackStart));
    }
}

void Trapeze::exeSwingSlideDownStart() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("TrapezeSlideDownStart");
    }

    if (!updateSlideDown() && MR::isBckStopped(mRider)) {
        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingSlideDown));
    }
}

void Trapeze::exeSwingSlideDown() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("TrapezeSlideDown");
    }

    updateSlideDown();
}

void Trapeze::exeSwingFrontStart() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingFrontStart))) {
            MR::startBckPlayer("TrapezeSlowSwingFrontStart", ::sAnimInterpoleFrame);
        } else {
            MR::startBckPlayer("TrapezeSwingFrontStart", ::sAnimInterpoleFrame);
        }
    }

    if (updateSwing()) {
        return;
    }

    if (MR::isBckStopped(mRider) && !(mIsSwingFront && mSwingVel < 0.0f)) {
        if (!mIsSwingFront && mSwingVel < ::sSpeedMinToKeepFreeSwing) {
            setNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingFrontEnd));
            return;
        }

        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingFrontEnd));
        return;
    }

    if (isStartSwingFront() && isNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingFrontStart))) {
        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingFrontStart));
        return;
    }

    if (isStartSwingBack()) {
        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingBackStart));
        return;
    }

    if (!mIsSwingFront && mSwingVel < 0.0f) {
        if (isNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingFrontStart))) {
            setNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingBackEnd));
        } else {
            setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingBackEnd));
        }
    }
}

void Trapeze::exeSwingFrontEnd() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingFrontEnd))) {
            MR::startBckPlayer("TrapezeSlowSwingFrontEnd", ::sAnimInterpoleFrame);
        } else {
            MR::startBckPlayer("TrapezeSwingFrontEnd", ::sAnimInterpoleFrame);
        }
    }

    if (updateSwing()) {
        return;
    }

    if (mSwingVel <= 0.0f) {
        if (isNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingFrontEnd))) {
            setNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingBackStart));
        } else {
            setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingBackStart));
        }
        return;
    }

    if (isStartSwingFront()) {
        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingFrontStart));
        return;
    }

    if (isStartSwingBack() && isNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingFrontEnd))) {
        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingBackStart));
    }
}

void Trapeze::exeSwingBackStart() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingBackStart))) {
            MR::startBckPlayer("TrapezeSlowSwingBackStart", ::sAnimInterpoleFrame);
        } else {
            MR::startBckPlayer("TrapezeSwingBackStart", ::sAnimInterpoleFrame);
        }
    }

    if (updateSwing()) {
        return;
    }

    if (MR::isBckStopped(mRider) && !(mIsSwingBack && mSwingVel > 0.0f)) {
        if (!mIsSwingBack && mSwingVel > -::sSpeedMinToKeepFreeSwing) {
            setNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingBackEnd));
            return;
        }

        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingBackEnd));
        return;
    }

    if (isStartSwingBack() && isNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingBackStart))) {
        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingBackStart));
        return;
    }

    if (isStartSwingFront()) {
        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingFrontStart));
        return;
    }

    if (!mIsSwingBack && mSwingVel > 0.0f) {
        if (isNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingBackStart))) {
            setNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingFrontEnd));
        } else {
            setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingFrontEnd));
        }
    }
}

void Trapeze::exeSwingBackEnd() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingBackEnd))) {
            MR::startBckPlayer("TrapezeSlowSwingBackEnd", ::sAnimInterpoleFrame);
        } else {
            MR::startBckPlayer("TrapezeSwingBackEnd", ::sAnimInterpoleFrame);
        }
    }

    if (updateSwing()) {
        return;
    }

    if (mSwingVel >= 0.0f) {
        if (isNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingBackEnd))) {
            setNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingFrontStart));
        } else {
            setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingFrontStart));
        }
        return;
    }

    if (isStartSwingBack()) {
        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingBackStart));
        return;
    }

    if (isStartSwingFront() && isNerve(GET_NERVE(Trapeze, TrapezeNrvSlowSwingBackEnd))) {
        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingFrontStart));
    }
}

void Trapeze::updateHitSensor(HitSensor* pSensor) {
    if (MR::isSensorAutoRush(pSensor)) {
        if (isNerve(GET_NERVE(Trapeze, TrapezeNrvStop))) {
            pSensor->mPosition.set< f32 >(mPosition.x, MR::clamp(MR::getPlayerPos()->y, mPosition.y - mRopeLength, mPosition.y), mPosition.z);
        } else if (isNerve(GET_NERVE(Trapeze, TrapezeNrvFree))) {
            MR::calcPerpendicFootToLineInside(&pSensor->mPosition, *MR::getPlayerPos(), mPosition, mStickPoint->mPosition);
        }
        return;
    }

    if (mRider != nullptr) {
        pSensor->mPosition.set(mRider->mPosition);
        pSensor->mPosition.add(mHangPoint->mUp * ::sSensorOffsetY);
    }
}

void Trapeze::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mRider != nullptr) {
        MR::tryGetItem(pSender, pReceiver);
    }
}

bool Trapeze::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgAutoRushBegin(msg)) {
        if ((!isNerve(GET_NERVE(Trapeze, TrapezeNrvStop)) && !isNerve(GET_NERVE(Trapeze, TrapezeNrvFree))) || MR::isOnGroundPlayer() ||
            pSender->mHost->mPosition.y > mPosition.y) {
            return false;
        }

        Vec3f posDiff = pSender->mHost->mPosition;
        posDiff.sub(pReceiver->mPosition);

        if (MR::abs(posDiff.dot(mFront)) > ::sDistanceStartBindZ) {
            return false;
        }

        mRider = pSender->mHost;

        Vec3f grabPos = pSender->mHost->mPosition;
        grabPos.sub(mPosition);

        f32 dotUp = mUp.dot(grabPos);
        f32 dotFront = mFront.dot(grabPos);

        grabPos = mPosition + mUp * dotUp + mFront * dotFront;

        mGrabCoord = MR::clamp(grabPos.distance(mPosition), 0.0f, mRopeLength);

        Vec3f frontVec;
        MR::getPlayerFrontVec(&frontVec);
        mSwingReverse = frontVec.dot(mFront) < 0.0f;

        f32 projGrabPointVel = mHangPoint->mVelocity.dot(mFront);
        f32 vel = MR::getPlayerVelocity()->dot(mFront);

        Vec3f swingVel = mFront;
        // A slow player takes the hang point's speed, but only when it swings forward (no abs on it).
        if (MR::abs(vel) < cNum_receiveOtherMsg_f1 && cNum_receiveOtherMsg_f2 < projGrabPointVel) {
            vel = projGrabPointVel;
        }
        swingVel.scale(vel);

        Vec3f grabFront = mFront;
        if (MR::abs(mPosition.y - grabPos.y) < ::sTransDiffMinY) {
            grabFront.set< f32 >(0.0f, -1.0f, 0.0f);
        }

        mHangPoint->mFront.set(grabFront);
        mHangPoint->setInfo(grabPos, swingVel, mPosition, mGrabCoord);
        mHangPoint->mUp.set(mUp);

        MR::startBckPlayer("TrapezeWait");
        MR::invalidateClipping(this);
        getSensor("body")->validate();
        getSensor("bind")->invalidate();

        MR::startSound(mRider, "SE_PV_CATCH");
        MR::startSound(mRider, "SE_PM_GRAB_OBJ");

        MR::startActorCameraNoTarget(this, mCameraInfo);

        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingSlideDownStart));
        return true;
    }

    if (MR::isMsgUpdateBaseMtx(msg)) {
        Vec3f side = mHangPoint->mSide;
        Vec3f front = mHangPoint->mFront;
        if (mSwingReverse) {
            side.scale(-1.0f);
            front.scale(-1.0f);
        }
        Pos3f mtx;
        mtx.setXYZDir(side, mHangPoint->mUp, front);
        mtx.setTrans(mHangPoint->mPosition);
        MR::setBaseTRMtx(mRider, mtx);
        return true;
    }

    if (MR::isMsgRushCancel(msg)) {
        mRider = nullptr;
        mGrabCoord = 0.0f;
        setNerve(GET_NERVE(Trapeze, TrapezeNrvFree));
        return true;
    }

    return false;
}

bool Trapeze::tryJump() {
    if (MR::isPressedJump(PLAYER_1) || MR::testSystemTriggerA()) {
        Vec3f jumpVel = mHangPoint->mVelocity * ::sJumpSpeedRate;
        jumpVel.y = MR::clamp(jumpVel.y, ::sJumpSpeedMinY, ::sJumpSpeedMaxY);

        Vec3f grabVel(jumpVel.x, 0.0f, jumpVel.z);
        if (grabVel.squared() > cNum_tryJump_f1) {
            MR::normalize(&grabVel);
            grabVel.scale(::sJumpSpeedMaxXZ);
            jumpVel.x = grabVel.x;
            jumpVel.z = grabVel.z;
        } else if (grabVel.squared() < ::sJumpSpeedMinXZ * ::sJumpSpeedMinXZ && MR::isMoveStickOperated(PLAYER_1)) {
            Vec3f stick(0.0f, 0.0f, 0.0f);
            MR::calcWorldStickDirectionXZ(&stick, PLAYER_1);
            if (stick.dot(mFront) > 0.0f) {
                jumpVel.x = mFront.x * ::sJumpSpeedMinXZ;
                jumpVel.z = mFront.z * ::sJumpSpeedMinXZ;
            } else {
                jumpVel.x = -mFront.x * ::sJumpSpeedMinXZ;
                jumpVel.z = -mFront.z * ::sJumpSpeedMinXZ;
            }
        }

        Vec3f frontDir = mFront;
        if (mSwingReverse) {
            frontDir.scale(-1.0f);
        }

        if (frontDir.dot(jumpVel) >= 0.0f) {
            MR::startBckPlayer("TrapezeFrontSpin");
        } else {
            MR::startBckPlayer("TrapezeBackSpin");
        }

        MR::setPlayerFrontTargetVec(frontDir, 1);
        MR::endActorCameraAtLanding(this, mCameraInfo);

        getSensor("body")->invalidate();
        getSensor("bind")->validate();

        MR::startSound(mRider, "SE_PV_JUMP_JOY");
        MR::startSound(mRider, "SE_PM_JUMP_M");

        MR::endBindAndPlayerWeakGravityJump(this, jumpVel);
        mRider = nullptr;

        setNerve(GET_NERVE(Trapeze, TrapezeNrvFreeInvalid));
        return true;
    }

    return false;
}

void Trapeze::updateStick(const Vec3f& rAnchor, f32 length) {
    mStickPoint->addAccel(mGravity * ::sStickPointGravity);
    mStickPoint->strain(rAnchor, length);
    mStickPoint->updatePosAndAxis(mStickPoint->mFront, ::sStickPointFrictionRate);
}

void Trapeze::updateStickMtx() {
    mPosMtx.setXYZDir(mStickPoint->mSide, mStickPoint->mUp, mStickPoint->mFront);
    mPosMtx.setTrans(mStickPoint->mPosition);
}

void Trapeze::updateHangPoint() {
    if (mGrabCoord < mRopeLength) {
        mGrabCoord += ::sHangAccelRate;
        mGrabCoord = mGrabCoord >= mRopeLength ? mRopeLength : mGrabCoord;
    }

    mWasSwingFront = mIsSwingFront;
    mWasSwingBack = mIsSwingBack;
    mIsSwingFront = false;
    mIsSwingBack = false;

    if (MR::isMoveStickOperated(PLAYER_1)) {
        Vec3f stick(0.0f, 0.0f, 0.0f);
        MR::calcWorldStickDirectionXZ(&stick, PLAYER_1);
        Vec3f frontAccel = mFront;
        if (stick.dot(mFront) < 0.0f) {
            frontAccel.scale(-1.0f);
        }
        MR::vecKillElement(frontAccel, mSide, &frontAccel);
        frontAccel.scale(::sHangCoordSpeed);
        mHangPoint->addAccel(frontAccel);

        if (frontAccel.dot(mFront) > 0.0f) {
            if (mSwingReverse) {
                mIsSwingBack = true;
            } else {
                mIsSwingFront = true;
            }
        } else {
            if (mSwingReverse) {
                mIsSwingFront = true;
            } else {
                mIsSwingBack = true;
            }
        }
    }

    mHangPoint->addAccel(mGravity * ::sHangPointGravity);
    mHangPoint->strain(mPosition, mGrabCoord);
    MR::vecKillElement(mHangPoint->mVelocity, mSide, &mHangPoint->mVelocity);
    mHangPoint->updatePosAndAxis(mHangPoint->mFront, ::sHangPointFrictionRate);

    mSwingVel = mHangPoint->mVelocity.dot(mHangPoint->mFront);
    if (mSwingReverse) {
        mSwingVel *= -1.0f;
    }

    s32 soundLvl = (mHangPoint->mVelocity.length() / cNum_updateHangPoint_f1) * cNum_updateHangPoint_f2;
    if (soundLvl > 100) {
        soundLvl = 100;
    }
    if (soundLvl < 0) {
        soundLvl = 0;
    }
    MR::startLevelSound(this, "SE_OJ_LV_ROPE_SWING_WIND_1", soundLvl);
    MR::startLevelSound(this, "SE_OJ_LV_ROPE_SWING_WIND_2", soundLvl);

    if (mPrevSoundLvl < 20 && 20 <= soundLvl) {
        MR::startSound(this, "SE_OJ_ROPE_CREAK_H");
    }
    mPrevSoundLvl = soundLvl;
}

bool Trapeze::updateBind() {
    if (tryJump()) {
        return true;
    }

    updateHangPoint();

    if (mGrabCoord < mRopeLength) {
        updateStick(mHangPoint->mPosition, mRopeLength - mGrabCoord);
    } else {
        mStickPoint->setPosAndAxis(mHangPoint->mPosition, mHangPoint->mSide, mHangPoint->mUp, mHangPoint->mFront);
        mStickPoint->mVelocity.zero();
    }

    updateStickMtx();
    return false;
}

bool Trapeze::updateSwing() {
    if (updateBind()) {
        return true;
    }

    if (!mIsSwingFront && !mIsSwingBack && MR::abs(mSwingVel) < ::sSpeedToSwingWait && mHangPoint->mUp.y >= ::sAxisToSwingWait) {
        setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingWait));
        return true;
    }

    return false;
}

bool Trapeze::updateSlideDown() {
    if (updateSwing()) {
        return true;
    }

    MR::startLevelSound(mRider, "SE_OJ_LV_SLIDE_ROPE_SLIDE");

    if (mGrabCoord == mRopeLength) {
        if (MR::isGreaterStep(this, ::sStepSlideToRumble)) {
            MR::startSound(mRider, "SE_PM_GRAB_OBJ");
        }

        if (mSwingVel > 0) {
            setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingFrontStart));
        } else {
            setNerve(GET_NERVE(Trapeze, TrapezeNrvSwingBackStart));
        }
        return true;
    }

    return false;
}

bool Trapeze::isStartSwingFront() const {
    return mIsSwingFront && !mWasSwingFront;
}

bool Trapeze::isStartSwingBack() const {
    return mIsSwingBack && !mWasSwingBack;
}

namespace {
    void drawPolygon0(TypedDraw& rDraw, const Vec3f& rPosA, const Vec3f& rPosB, const Vec3f& rSide, const Vec3f& rFront, f32 x1, f32 y1, f32 x2, f32 y2, f32 texA,
                      f32 texB) {
        VertexPCT* vtx = rDraw.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
        {
            *vtx++ = {rPosA.x - x1 * rSide.x + y1 * rFront.x, rPosA.y - x1 * rSide.y + y1 * rFront.y, rPosA.z - x1 * rSide.z + y1 * rFront.z, Rgba8(::sColorPlusZ), 0.0f, texA};

            *vtx++ = {rPosA.x + x2 * rSide.x - y2 * rFront.x, rPosA.y + x2 * rSide.y - y2 * rFront.y, rPosA.z + x2 * rSide.z - y2 * rFront.z, Rgba8(::sColorPlusX), 1.0f, texA};

            *vtx++ = {rPosB.x - x1 * rSide.x + y1 * rFront.x, rPosB.y - x1 * rSide.y + y1 * rFront.y, rPosB.z - x1 * rSide.z + y1 * rFront.z, Rgba8(::sColorPlusZ), 0.0f, texB};

            *vtx++ = {rPosB.x + x2 * rSide.x - y2 * rFront.x, rPosB.y + x2 * rSide.y - y2 * rFront.y, rPosB.z + x2 * rSide.z - y2 * rFront.z, Rgba8(::sColorPlusX), 1.0f, texB};
        }
    }

    void drawPolygon1(TypedDraw& rDraw, const Vec3f& rPosA, const Vec3f& rPosB, const Vec3f& rSide, const Vec3f& rFront, f32 x1, f32 y1, f32 x2, f32 y2, f32 texA,
                      f32 texB) {
        VertexPCT* vtx = rDraw.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
        {
            *vtx++ = {rPosA.x - x1 * rSide.x - y1 * rFront.x, rPosA.y - x1 * rSide.y - y1 * rFront.y, rPosA.z - x1 * rSide.z - y1 * rFront.z, Rgba8(::sColorMinusX), 0.0f, texA};

            *vtx++ = {rPosA.x + x2 * rSide.x + y2 * rFront.x, rPosA.y + x2 * rSide.y + y2 * rFront.y, rPosA.z + x2 * rSide.z + y2 * rFront.z, Rgba8(::sColorPlusZ), 1.0f, texA};

            *vtx++ = {rPosB.x - x1 * rSide.x - y1 * rFront.x, rPosB.y - x1 * rSide.y - y1 * rFront.y, rPosB.z - x1 * rSide.z - y1 * rFront.z, Rgba8(::sColorMinusX), 0.0f, texB};

            *vtx++ = {rPosB.x + x2 * rSide.x + y2 * rFront.x, rPosB.y + x2 * rSide.y + y2 * rFront.y, rPosB.z + x2 * rSide.z + y2 * rFront.z, Rgba8(::sColorPlusZ), 1.0f, texB};
        }
    }

    void drawPolygon2(TypedDraw& rDraw, const Vec3f& rPosA, const Vec3f& rPosB, const Vec3f& rSide, const Vec3f& rFront, f32 x1, f32 y1, f32 x2, f32 y2, f32 texA,
                      f32 texB) {
        VertexPCT* vtx = rDraw.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
        {
            *vtx++ = {rPosA.x + x1 * rSide.x - y1 * rFront.x, rPosA.y + x1 * rSide.y - y1 * rFront.y, rPosA.z + x1 * rSide.z - y1 * rFront.z, Rgba8(::sColorPlusX), 0.0f, texA};

            *vtx++ = {rPosA.x - x2 * rSide.x - y2 * rFront.x, rPosA.y - x2 * rSide.y - y2 * rFront.y, rPosA.z - x2 * rSide.z - y2 * rFront.z, Rgba8(::sColorMinusX), 1.0f, texA};

            *vtx++ = {rPosB.x + x1 * rSide.x - y1 * rFront.x, rPosB.y + x1 * rSide.y - y1 * rFront.y, rPosB.z + x1 * rSide.z - y1 * rFront.z, Rgba8(::sColorPlusX), 0.0f, texB};

            *vtx++ = {rPosB.x - x2 * rSide.x - y2 * rFront.x, rPosB.y - x2 * rSide.y - y2 * rFront.y, rPosB.z - x2 * rSide.z - y2 * rFront.z, Rgba8(::sColorMinusX), 1.0f, texB};
        }
    }
};  // namespace

void Trapeze::drawRope(TypedDraw& rDraw, const Vec3f& rPosA, const Vec3f& rPosB, const Vec3f& rSide, const Vec3f& rFront, f32 texA, f32 texB) const {
    ::drawPolygon0(rDraw, rPosA, rPosB, rSide, rFront, ::sDrawWidthX + ::sDrawWidthShortOffsetX, ::sDrawWidthZ + ::sDrawWidthShortOffsetZ,
                   ::sDrawWidthX + ::sDrawWidthLongOffsetX, ::sDrawWidthZ + ::sDrawWidthShortOffsetZ, texA + ::sTexRateDiff0, texB + ::sTexRateDiff0);

    ::drawPolygon1(rDraw, rPosA, rPosB, rSide, rFront, ::sDrawWidthX + ::sDrawWidthLongOffsetX, ::sDrawWidthZ + ::sDrawWidthShortOffsetZ,
                   ::sDrawWidthX + ::sDrawWidthShortOffsetX, ::sDrawWidthZ + ::sDrawWidthShortOffsetZ, texA + ::sTexRateDiff1,
                   texB + ::sTexRateDiff1);

    ::drawPolygon2(rDraw, rPosA, rPosB, rSide, rFront, ::sDrawWidthX + ::sDrawWidthLongOffsetX, ::sDrawWidthShortOffsetZ,
                   ::sDrawWidthX + ::sDrawWidthLongOffsetX, ::sDrawWidthShortOffsetZ, texA + ::sTexRateDiff2, texB + ::sTexRateDiff2);
}

TrapezeRopeDrawInit::TrapezeRopeDrawInit(const char* pName) : NameObj(pName) {
    mTexture = new GameTexture(MR::loadTexFromArc("Trapeze", "TrapezeRope.bti"));

    mMaterial = makeMaterial();
}

SiteMaterial TrapezeRopeDrawInit::makeMaterial() const {
    // runtime/src/render/materials/tex_modulate.hlsl: the texture times the vertex colour, its alpha the texture's; drawn where
    // the alpha is above 0, depth tested and written, no culling.
    SiteMaterial material;
    material.init(NEBULA_SHADER_TEX_MODULATE, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, true, NEBULA_CULL_NONE);
    material.constants[0] = 1.0f;
    material.constants[1] = 0.0f;
    material.setTexture(0, mTexture);
    return material;
}
