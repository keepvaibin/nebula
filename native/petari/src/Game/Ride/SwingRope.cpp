#include "Game/Ride/SwingRope.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Ride/SledRopePoint.hpp"
#include "Game/Ride/SwingRopePoint.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include <nebula_math.h>
#include "Game/System/NebulaInput.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHangPoint_f1, "updateHangPoint.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHangPoint_f2, "updateHangPoint.f2", 100.0f);

namespace {
    NEBULA_TUNABLE(f32, sPointInterval, 50.0f);
    NEBULA_TUNABLE(f32, sDrawWidthX, 30.0f);
    NEBULA_TUNABLE(f32, sDrawWidthZ, 40.0f);
    NEBULA_TUNABLE(f32, sDrawWidthLongOffsetX, 13.0f);
    NEBULA_TUNABLE(f32, sDrawWidthShortOffsetX, 3.0f);
    NEBULA_TUNABLE(f32, sDrawWidthShortOffsetZ, 3.0f);
    NEBULA_TUNABLE(f32, sTexRate, 0.13f);
    NEBULA_TUNABLE(f32, sHangAccelRateLoose, 0.8f);
    NEBULA_TUNABLE(f32, sHangAccelRateStretch, 0.5f);
    NEBULA_TUNABLE(f32, sHangPointGravityLoose, 0.6f);
    NEBULA_TUNABLE(f32, sHangPointGravityStretcn, 0.8f);
    NEBULA_TUNABLE(f32, sHangPointFrictionRate, 0.995f);
    NEBULA_TUNABLE(f32, sHangPointReboundRate, 0.7f);
    NEBULA_TUNABLE(s32, sAnimInterpoleFrame, 10);
    NEBULA_TUNABLE(s32, sAnimInterpoleFrameToSlowWait, 10);
    NEBULA_TUNABLE(s32, sStepWaitAfterSwing, 0);
    NEBULA_TUNABLE(f32, sHangPointDownAccel, 0.5f);
    NEBULA_TUNABLE(f32, sHangPointDownSpeedMax, -10.0f);
    NEBULA_TUNABLE(f32, sHangCoordMin, 100.0f);
    NEBULA_TUNABLE(f32, sJumpSpeedRate, 5.0f);
    NEBULA_TUNABLE(f32, sJumpSpeedMinXZ, 5.0f);
    NEBULA_TUNABLE(f32, sJumpSpeedMaxXZ, 35.0f);
    NEBULA_TUNABLE(f32, sJumpSpeedMinY, 25.0f);
    NEBULA_TUNABLE(f32, sJumpSpeedMaxY, 50.0f);
    NEBULA_TUNABLE(f32, sBindPointFrictionRateHead, 0.7f);
    NEBULA_TUNABLE(f32, sBindPointFrictionRateTail, 0.8f);
    NEBULA_TUNABLE(s32, sStepFixUpperPointPos, 10);
    NEBULA_TUNABLE(f32, sCoordStretchToLoose, 5.0f);
    NEBULA_TUNABLE(f32, sCoordLooseToStretch, 1.0f);
    NEBULA_TUNABLE(f32, sPointGravity, 1.0f);
    NEBULA_TUNABLE(f32, sFreePointFrictionRateHead, 0.8f);
    NEBULA_TUNABLE(f32, sFreePointFrictionRateTail, 0.92f);
    NEBULA_TUNABLE(f32, sPointSpeedToStop, 0.1f);
    NEBULA_TUNABLE(f32, sPointNormalToStop, 0.9999f);
    NEBULA_TUNABLE(f32, sTransDiffMinY, 1.0f);
    NEBULA_TUNABLE(f32, sSensorRiderOffsetY, -50.0f);
    NEBULA_TUNABLE(f32, sFootPosOffsetY, -20.0f);
    NEBULA_TUNABLE(f32, sFootPosOffsetZ, 10.0f);
    NEBULA_TUNABLE(f32, sHangPointBlendRateAxisZ, 0.1f);
    NEBULA_TUNABLE(f32, sPointBlendRateAxisZ, 0.5f);
    NEBULA_TUNABLE(f32, sFootPointAxisBlendRate, 0.3f);
};  // namespace

namespace NrvSwingRope {
    NEW_NERVE(SwingRopeNrvStop, SwingRope, Stop);
    NEW_NERVE(SwingRopeNrvFree, SwingRope, Free);
    NEW_NERVE(SwingRopeNrvFreeInvalid, SwingRope, FreeInvalid);
    NEW_NERVE(SwingRopeNrvBindSlideDownStart, SwingRope, BindSlideDownStart);
    NEW_NERVE(SwingRopeNrvBindSlideDown, SwingRope, BindSlideDown);
    NEW_NERVE(SwingRopeNrvBindStretch, SwingRope, BindStretch);
    NEW_NERVE(SwingRopeNrvBindLoose, SwingRope, BindLoose);

};  // namespace NrvSwingRope

namespace {
    static Color8 sColorPlusZ(0xFF, 0xFF, 0xFF, 0xFF);
    static Color8 sColorPlusX(0xFF, 0xFF, 0xFF, 0xFF);
    static Color8 sColorMinusX(0xFF, 0xFF, 0xFF, 0xFF);

    NEBULA_TUNABLE(f32, sHangCoordMaxOffset, 200.0f);
    NEBULA_TUNABLE(f32, sFootPosOffsetX, 0.0f);
};  // namespace

SwingRope::SwingRope(const char* pName)
    : LiveActor(pName), mBasePos(0.0f, 0.0f, 0.0f), mRopeLength(), mIsStretched(), mStretchTime(), mAccelTime(), mNumPoints(), mPoints(), mRider(),
      mGrabCoord(), mGrabPointNum(), mSlideSpeed(), mSledPoint(), mCameraInfo(), mFootPos(0.0f, 0.0f, 0.0f),
      mShadowDropPos(0.0f, 0.0f, 0.0f), mPrevSoundLvl() {
    mRideMtx.identity();
}

void SwingRope::init(const TableIter& rIter) {
    MR::createSceneObj(SceneObj_SwingRopeGroup);
    MR::connectToScene(this, MR::MovementType_Ride, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_SwingRope);
    MR::initDefaultPos(this, rIter);

    mBasePos.set(mPosition);
    mRopeLength = mScale.y * cNum_init_f1;

    MR::calcGravity(this);
    initPoints();

    initHitSensor(2);
    MR::addHitSensorCallbackBinder(this, "bind", 8, cNum_init_f2);
    MR::addHitSensorCallbackRide(this, "body", 8, cNum_init_f3);

    initSound(4, false);

    MR::setClippingTypeSphere(this, mRopeLength);

    MR::initShadowSurfaceCircle(this, cNum_init_f4);
    MR::onCalcShadow(this, nullptr);
    MR::setShadowDropPositionPtr(this, nullptr, &mShadowDropPos);

    MR::initActorCamera(this, rIter, &mCameraInfo);
    initNerve(GET_NERVE(SwingRope, SwingRopeNrvFree));
    makeActorAppeared();
}

void SwingRope::draw() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    // A held rope draws through its points like a free one.
    TypedDraw site(MR::getSceneObj< SwingRopeGroup >(SceneObj_SwingRopeGroup)->mMaterial, nullptr, MR::getCameraViewMtx());
    if (isNerve(GET_NERVE(SwingRope, SwingRopeNrvStop))) {
        drawStop(site);
    } else {
        drawFree(site);
    }
}

void SwingRope::exeStop() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
    }

    s32 index = mNumPoints - 1;
    mShadowDropPos.x = mPoints[index]->mPosition.x;
    mShadowDropPos.y = mBasePos.y - mRopeLength;
    mShadowDropPos.z = mPoints[index]->mPosition.z;
}

void SwingRope::exeFree() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
    }

    addPointGravity();
    restrictPointToTail(0, nullptr);

    Vec3f front = mPoints[0]->mFront;

    SwingRopePoint* point;
    for (s32 idx = 0; idx < mNumPoints; idx++) {
        point = mPoints[idx];
        point->updatePosAndAxis(front, calcFriction(idx));
        front.set(mPoints[idx]->mFront);
    }

    s32 index = mNumPoints - 1;
    mShadowDropPos.x = mPoints[index]->mPosition.x;
    mShadowDropPos.y = mBasePos.y - mRopeLength;
    mShadowDropPos.z = mPoints[index]->mPosition.z;

    if (isAllPointsStop()) {
        for (s32 idx = 0; idx < mNumPoints; idx++) {
            mPoints[idx]->stop();
        }
        setNerve(GET_NERVE(SwingRope, SwingRopeNrvStop));
    }
}

void SwingRope::exeFreeInvalid() {
    exeFree();

    if (MR::isOnGroundPlayer()) {
        setNerve(GET_NERVE(SwingRope, SwingRopeNrvFree));
    }
}

void SwingRope::exeBindSlideDownStart() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("SwingRopeSlideDownStart", 0);
        mSlideSpeed = 0.0f;
    }

    if (!updateSlideDown() && MR::isBckStopped(mRider)) {
        setNerve(GET_NERVE(SwingRope, SwingRopeNrvBindSlideDown));
    }
}

void SwingRope::exeBindSlideDown() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("SwingRopeSlideDown", ::sAnimInterpoleFrame);
    }

    updateSlideDown();
}

void SwingRope::exeBindStretch() {
    if (MR::isFirstStep(this)) {
        mStretchTime = 0;
    }

    if (!updateStretch() && mGrabCoord - mBasePos.distance(mSledPoint->mPosition) > ::sCoordStretchToLoose) {
        setNerve(GET_NERVE(SwingRope, SwingRopeNrvBindLoose));
    }
}

void SwingRope::exeBindLoose() {
    if (updateLoose()) {
        return;
    }

    if (isStretched()) {
        setNerve(GET_NERVE(SwingRope, SwingRopeNrvBindStretch));
    }
}

void SwingRope::initPoints() {
    mNumPoints = mRopeLength / ::sPointInterval;
    // At least two: the head is read every frame and friction divides by mNumPoints - 1.
    if (mNumPoints < 2) {
        mNumPoints = 2;
    }
    mPoints = new SwingRopePoint*[mNumPoints];

    for (s32 idx = 0; idx < mNumPoints; idx++) {
        Vec3f pos = mGravity * (::sPointInterval * (idx + 1));
        pos.add(mBasePos);
        mPoints[idx] = new SwingRopePoint(pos);
    }

    mSledPoint = new SledRopePoint(mBasePos, ::sPointInterval);
}

void SwingRope::updateHitSensor(HitSensor* pSensor) {
    if (MR::isSensorBinder(pSensor)) {
        Vec3f bottomPos = mGravity;
        bottomPos.scale(mRopeLength);
        bottomPos.add(mBasePos);
        MR::calcPerpendicFootToLineInside(&pSensor->mPosition, *MR::getPlayerPos(), mBasePos, bottomPos);
        return;
    }

    if (MR::isSensorRide(pSensor)) {
        Vec3f sensorPos = mSledPoint->mUp * ::sSensorRiderOffsetY;
        sensorPos.add(mSledPoint->mPosition);
        pSensor->mPosition.set(sensorPos);
        return;
    }
}

void SwingRope::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mRider == nullptr) {
        return;
    }

    MR::tryGetItem(pSender, pReceiver);
}

bool SwingRope::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgAutoRushBegin(msg)) {
        if (MR::isOnGroundPlayer() || MR::isPlayerSwimming() ||
            (!isNerve(GET_NERVE(SwingRope, SwingRopeNrvStop)) && !isNerve(GET_NERVE(SwingRope, SwingRopeNrvFree)))) {
            return false;
        }

        mRider = pSender->mHost;
        mIsStretched = false;
        MR::calcGravity(this, *MR::getPlayerPos());

        Vec3f grav = mGravity;
        Vec3f pos = mPosition;

        MR::calcPerpendicFootToLine(&pos, *MR::getPlayerPos(), mBasePos, mBasePos + grav * mRopeLength);

        Vec3f posDiff = pos - mBasePos;
        mGrabCoord = MR::clamp(posDiff.dot(grav), ::sHangCoordMin, mRopeLength - ::sHangCoordMaxOffset);
        mGrabPointNum = calcPointNo(mGrabCoord);

        Vec3f front;
        MR::getPlayerFrontVec(&front);

        if (MR::abs(mRider->mPosition.y - mBasePos.y) < ::sTransDiffMinY) {
            front.set< f32 >(0.0f, -1.0f, 0.0f);
        }
        mSledPoint->mFront.set(front);

        const Vec3f& riderPos = mRider->mPosition;
        const Vec3f& vel = *MR::getPlayerVelocity();
        mSledPoint->setInfo(riderPos, vel, mBasePos, mGrabCoord);

        updateRideMtx();
        updateFootPos();

        MR::invalidateClipping(this);
        MR::startSound(mRider, "SE_PV_CATCH");
        MR::startSound(mRider, "SE_PM_GRAB_OBJ");
        MR::startActorCameraNoTarget(this, mCameraInfo);

        setNerve(GET_NERVE(SwingRope, SwingRopeNrvBindSlideDownStart));

        return true;
    }

    if (MR::isMsgUpdateBaseMtx(msg)) {
        MR::setBaseTRMtx(mRider, mRideMtx);
        return true;
    }

    if (MR::isMsgRushCancel(msg)) {
        mRider = nullptr;
        mGrabCoord = 0.0f;
        mGrabPointNum = 0.0f;
        setNerve(GET_NERVE(SwingRope, SwingRopeNrvFree));
        return true;
    }

    return false;
}

f32 SwingRope::calcPointNo(f32 coord) const {
    return MR::clamp((coord / ::sPointInterval) - 1.0f, 0.0f, mNumPoints - 1.0f);
}

f32 SwingRope::calcFriction(s32 index) const {
    if (mRider != nullptr) {
        return (index * (::sBindPointFrictionRateTail - ::sBindPointFrictionRateHead)) / (mNumPoints - 1) + ::sBindPointFrictionRateHead;
    } else {
        return (index * (::sFreePointFrictionRateTail - ::sFreePointFrictionRateHead)) / (mNumPoints - 1) + ::sFreePointFrictionRateHead;
    }
}

void SwingRope::addPointGravity() {
    Vec3f grav = mGravity * ::sPointGravity;

    for (s32 idx = 0; idx < mNumPoints; idx++) {
        mPoints[idx]->addAccel(grav);
    }
}

void SwingRope::restrictPointToTail(s32 index, const Vec3f* pAnchor, f32 length) {
    if (length == 0.0f) {
        length = ::sPointInterval;
    }

    Vec3f anchor;
    if (pAnchor != nullptr) {
        anchor.set(*pAnchor);
    } else {
        anchor.set(mBasePos);
    }

    for (; index < mNumPoints; index++) {
        mPoints[index]->restrict(anchor, length, nullptr);
        anchor.set(mPoints[index]->mPosition);
        anchor.add(mPoints[index]->mVelocity);
        length = ::sPointInterval;
    }
}

void SwingRope::restrictPointToHead(s32 index, const Vec3f& rAnchor, f32 length) {
    Vec3f anchor = rAnchor;

    for (; index >= 0; index--) {
        mPoints[index]->restrict(anchor, length, nullptr);
        anchor.set(mPoints[index]->mPosition);
        anchor.add(mPoints[index]->mVelocity);
        length = ::sPointInterval;
    }
}

bool SwingRope::isAllPointsStop() const {
    for (s32 idx = 0; idx < mNumPoints; idx++) {
        if (MR::abs(mPoints[idx]->mVelocity.x) > ::sPointSpeedToStop || MR::abs(mPoints[idx]->mVelocity.y) > ::sPointSpeedToStop ||
            MR::abs(mPoints[idx]->mVelocity.z) > ::sPointSpeedToStop) {
            return false;
        }

        if (mPoints[idx]->mUp.y < ::sPointNormalToStop) {
            return false;
        }
    }

    return true;
}

bool SwingRope::isStretched() const {
    return MR::abs(mGrabCoord - mBasePos.distance(mSledPoint->mPosition)) < ::sCoordLooseToStretch;
}

bool SwingRope::tryJump() {
    if (MR::isPressedJump(PLAYER_1) || MR::testSystemTriggerA()) {
        Vec3f front = mSledPoint->mFront;

        Vec3f jumpY = mGravity;
        jumpY.scale(mSledPoint->mVelocity.dot(mGravity));
        if (jumpY.length() < ::sJumpSpeedMinY) {
            jumpY = mGravity * -::sJumpSpeedMinY;
        } else if (jumpY.length() > ::sJumpSpeedMaxY) {
            jumpY = mGravity * -::sJumpSpeedMaxY;
        }

        Vec3f jumpXZ = mSledPoint->mVelocity;
        MR::vecKillElement(jumpXZ, mGravity, &jumpXZ);
        if (jumpXZ.length() > ::sJumpSpeedMaxXZ) {
            MR::normalize(&jumpXZ);
            front.set(jumpXZ);
            jumpXZ.scale(::sJumpSpeedMaxXZ);
        } else if (jumpXZ.length() < ::sJumpSpeedMinXZ && MR::isMoveStickOperated(PLAYER_1)) {
            Vec3f stick(0.0f, 0.0f, 0.0f);
            MR::calcWorldStickDirectionXZ(&stick, PLAYER_1);
            front.set(stick);
            jumpXZ.set(stick * ::sJumpSpeedRate);
            MR::vecKillElement(jumpXZ, mGravity, &jumpXZ);
        } else if (!MR::isNearZero(jumpXZ)) {
            front.set(jumpXZ);
            MR::normalize(&front);
        }

        Vec3f jumpVec = jumpXZ;
        jumpVec.add(jumpY);

        MR::endActorCameraAtLanding(this, mCameraInfo);
        MR::startSound(mRider, "SE_PV_JUMP_S");
        MR::startSound(mRider, "SE_PM_JUMP_M");
        MR::startBckPlayer("SwingRopeSpin", 0);

        MR::setPlayerFrontTargetVec(front, 1);
        MR::endBindAndPlayerWeakGravityJump(this, jumpVec);

        mRider = nullptr;
        setNerve(GET_NERVE(SwingRope, SwingRopeNrvFreeInvalid));
        return true;
    }

    return false;
}

void SwingRope::updateHangPoint() {
    Vec3f stick(0.0f, 0.0f, 0.0f);
    Vec3f front = mSledPoint->mFront;
    MR::calcWorldStickDirectionXZ(&stick, PLAYER_1);

    if (!MR::isNearZero(stick)) {
        MR::vecBlend(mSledPoint->mFront, stick, &front, ::sHangPointBlendRateAxisZ);
        if (isNerve(GET_NERVE(SwingRope, SwingRopeNrvBindLoose))) {
            stick.scale(::sHangAccelRateLoose);
        } else {
            stick.scale(::sHangAccelRateStretch);
        }
        mSledPoint->addAccel(stick);

        if (mAccelTime == 0) {
            MR::startBckPlayer("SwingRopeAccel", ::sAnimInterpoleFrame);
        } else if (MR::isBckOneTimeAndStopped(mRider)) {
            MR::startBckPlayer("SwingRopeSwingWait", ::sAnimInterpoleFrame);
        }
        mAccelTime++;
    } else {
        if (mAccelTime > ::sStepWaitAfterSwing) {
            MR::startBckPlayer("SwingRopeWait", ::sAnimInterpoleFrameToSlowWait);
            mAccelTime = 0;
        }
    }

    mSledPoint->bindToMapCollision(::sHangPointReboundRate);
    mPosition.set(mSledPoint->mPosition);
    MR::calcGravity(this);

    Vec3f grav = mGravity;
    if (isNerve(GET_NERVE(SwingRope, SwingRopeNrvBindLoose))) {
        grav.scale(::sHangPointGravityLoose);
    } else {
        grav.scale(::sHangPointGravityStretcn);
    }
    mSledPoint->addAccel(grav);
    mSledPoint->restrict(mBasePos, mGrabCoord, nullptr);
    mSledPoint->updatePosAndAxis(front, ::sHangPointFrictionRate);

    updateRideMtx();
    updateFootPos();
    mShadowDropPos.x = mSledPoint->mPosition.x;
    mShadowDropPos.z = mSledPoint->mPosition.z;

    s32 soundLvl = (mSledPoint->mVelocity.length() / cNum_updateHangPoint_f1) * cNum_updateHangPoint_f2;
    if (soundLvl > 100) {
        soundLvl = 100;
    }
    if (soundLvl < 0) {
        soundLvl = 0;
    }
    MR::startLevelSound(this, "SE_OJ_LV_ROPE_SWING_WIND_1", soundLvl);

    if (mPrevSoundLvl < 20 && 20 <= soundLvl) {
        MR::startSound(this, "SE_OJ_ROPE_CREAK_H");
    }
    mPrevSoundLvl = soundLvl;
}

void SwingRope::updateRideMtx() {
    mRideMtx.setTR(mSledPoint->mSide, mSledPoint->mUp, mSledPoint->mFront, mSledPoint->mPosition);
}

void SwingRope::updateFootPos() {
    Pos3f footMtx;
    MR::calcPlayerJointMtx(&footMtx, "FootL");

    Vec3f side, up, front;
    footMtx.getXDir(side);
    footMtx.getYDir(up);
    footMtx.getZDir(front);
    footMtx.getTrans(mFootPos);

    mFootPos.add(side * ::sFootPosOffsetX + up * ::sFootPosOffsetY + front * ::sFootPosOffsetZ);
}

bool SwingRope::updateStretch() {
    if (tryJump()) {
        return true;
    }

    updateHangPoint();
    addPointGravity();
    updateStretchHangUpperPoints();
    updateHangLowerPoints();
    mStretchTime++;

    return false;
}

bool SwingRope::updateLoose() {
    if (tryJump()) {
        return true;
    }

    updateHangPoint();
    updateLooseUpperPoints();
    updateHangLowerPoints();

    return false;
}

void SwingRope::updateLooseUpperPoints() {
    SwingRopePoint* point;

    addPointGravity();
    restrictPointToTail(0, nullptr);

    s32 grabIndex = mGrabPointNum;
    f32 length = mGrabCoord - (grabIndex + 1) * ::sPointInterval;
    Vec3f pos = mSledPoint->mPosition;
    restrictPointToHead(grabIndex, pos, length);
    restrictPointToTail(0, nullptr);

    Vec3f front = mSledPoint->mFront;
    for (s32 idx = 0; idx <= grabIndex; idx++) {
        point = mPoints[idx];
        point->updatePosAndAxis(front, calcFriction(idx));
        front.set(mPoints[idx]->mFront);
    }
}

bool SwingRope::updateSlideDown() {
    mSlideSpeed -= ::sHangPointDownAccel;
    mSlideSpeed = mSlideSpeed >= ::sHangPointDownSpeedMax ? mSlideSpeed : ::sHangPointDownSpeedMax;
    mGrabCoord -= mSlideSpeed;
    mGrabCoord = MR::clamp(mGrabCoord, ::sHangCoordMin, mRopeLength - ::sHangCoordMaxOffset);

    mGrabPointNum = calcPointNo(mGrabCoord);

    if (mIsStretched) {
        if (updateStretch()) {
            // this returns early if jumping, not if stretching
            return true;
        }
    } else {
        if (updateLoose()) {
            // this returns early if jumping, not if loose
            return true;
        }

        if (isStretched()) {
            mIsStretched = true;
            mStretchTime = 0;
        }
    }

    if (mGrabCoord >= mRopeLength - ::sHangCoordMaxOffset) {
        mGrabCoord = mRopeLength - ::sHangCoordMaxOffset;
        mSlideSpeed = 0.0f;
        setNerve(GET_NERVE(SwingRope, SwingRopeNrvBindStretch));
        return true;
    }

    return false;
}

void SwingRope::updateStretchHangUpperPoints() {
    if (mStretchTime < ::sStepFixUpperPointPos) {
        updateLooseUpperPoints();
    }

    s32 grabIndex = mGrabPointNum;
    s32 maxIndex = mNumPoints - 1;
    if (grabIndex < 0) {
        grabIndex = 0;
    } else if (grabIndex > maxIndex) {
        grabIndex = maxIndex;
    }

    Vec3f front = mSledPoint->mFront;
    for (s32 idx = 0; idx <= grabIndex; idx++) {
        Vec3f pos = mSledPoint->mUp * (-::sPointInterval * (idx + 1));
        pos.add(mBasePos);

        if (mStretchTime < ::sStepFixUpperPointPos) {
            f32 t = MR::getEaseOutValue(static_cast< f32 >(mStretchTime + 1) / ::sStepFixUpperPointPos, 0.0f, 1.0f, 1.0f);
            pos = pos * t + mPoints[idx]->mPosition * (1.0f - t);
        }
        mPoints[idx]->setAndUpdatePosAndAxis(pos, mSledPoint->mUp, front);
        front.set(mPoints[idx]->mFront);
    }
}

void SwingRope::updateHangLowerPoints() {
    s32 grabIndex = mGrabPointNum;
    s32 nextIndex = grabIndex + 1;

    if (nextIndex <= mNumPoints - 1) {
        mPoints[nextIndex]->setPosAndAxis(mFootPos, mSledPoint->mSide, mSledPoint->mUp, mSledPoint->mFront);

        Vec3f up, front, side;
        mPoints[nextIndex]->mVelocity.zero();
        restrictPointToTail(nextIndex + 1, &mFootPos);
        updateHangLowerPointPos(nextIndex + 1);

        s32 nextNextIndex = nextIndex + 1 < mNumPoints ? nextIndex + 1 : nextIndex;
        MR::vecBlend(mSledPoint->mUp, mPoints[nextNextIndex]->mUp, &up, ::sPointBlendRateAxisZ);
        MR::vecBlend(mSledPoint->mFront, mPoints[nextNextIndex]->mFront, &front, ::sPointBlendRateAxisZ);
        MR::normalize(&up);
        MR::normalize(&front);

        side.cross(up, front);
        if (!MR::isNearZero(side)) {
            MR::makeAxisUpFront(&side, &front, up, front);
            mPoints[nextIndex]->setPosAndAxis(mFootPos, side, up, front);
        }
    }
}

void SwingRope::updateHangLowerPointPos(s32 index) {
    for (; index < mNumPoints; index++) {
        SwingRopePoint* point = mPoints[index];
        SwingRopePoint* prevPoint = mPoints[index - 1];
        Vec3f front;
        MR::vecBlend(point->mFront, prevPoint->mFront, &front, ::sFootPointAxisBlendRate);
        point->updatePosAndAxis(front, calcFriction(index));
    }
}

namespace {
    void sendPoint(VertexPCT*& rVtx, const Vec3f& pos, const Vec3f& side, const Vec3f& front, f32 x, f32 y, u32 color, f32 texX, f32 texY) {
        *rVtx++ = {pos.x + side.x * x + front.x * y, pos.y + side.y * x + front.y * y, pos.z + side.z * x + front.z * y, Rgba8(color), texX, texY};
    }

    void drawPolygon(TypedDraw& rDraw, const Vec3f& posA, const Vec3f& posB, const Vec3f& side, const Vec3f& front, f32 x1, f32 y1, f32 x2, f32 y2, u32 color1,
                     u32 color2, f32 texY1, f32 texY2) {
        VertexPCT* vtx = rDraw.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
        {
            sendPoint(vtx, posA, side, front, x1, y1, color1, 0.0f, texY1);
            sendPoint(vtx, posA, side, front, x2, y2, color2, 1.0f, texY1);
            sendPoint(vtx, posB, side, front, x1, y1, color1, 0.0f, texY2);
            sendPoint(vtx, posB, side, front, x2, y2, color2, 1.0f, texY2);
        }
    }

    void drawLine(TypedDraw& rDraw, const SwingRope* pRope, u32 color1, u32 color2, f32 x1, f32 y1, f32 x2, f32 y2) {
        VertexPCT* vtx = rDraw.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, (pRope->mNumPoints + 1) * 2);
        {
            const Vec3f& front = pRope->mPoints[0]->getFront();
            const Vec3f& side = pRope->mPoints[0]->getSide();
            const Vec3f& pos = pRope->getBasePos();

            sendPoint(vtx, pos, side, front, x1, y1, color1, 0.0f, 0.0f);
            sendPoint(vtx, pos, side, front, x2, y2, color2, 1.0f, 0.0f);

            SwingRopePoint* point;
            for (s32 idx = 0; idx < pRope->mNumPoints; idx++) {
                point = pRope->mPoints[idx];
                const Vec3f& front = point->getFront();
                const Vec3f& side = point->getSide();
                const Vec3f& pos = point->getPosition();

                sendPoint(vtx, pos, side, front, x1, y1, color1, 0.0f, ::sTexRate * (idx + 1));
                sendPoint(vtx, pos, side, front, x2, y2, color2, 1.0f, ::sTexRate * (idx + 1));
            }
        }
    }

};  // namespace

void SwingRope::drawStop(TypedDraw& rDraw) const {
    Vec3f bottom(mBasePos.x, mBasePos.y - mRopeLength, mBasePos.z);
    const Vec3f& side = mPoints[0]->getSide();
    const Vec3f& front = mPoints[0]->getFront();

    f32 texY = (mRopeLength / ::sPointInterval) * ::sTexRate;

    ::drawPolygon(rDraw, mBasePos, bottom, side, front, -::sDrawWidthX, (::sDrawWidthZ + ::sDrawWidthShortOffsetZ), ::sDrawWidthX + ::sDrawWidthShortOffsetX,
                  -(::sDrawWidthZ + ::sDrawWidthShortOffsetZ), ::sColorPlusZ, ::sColorPlusX, 0.0f, texY);

    ::drawPolygon(rDraw, mBasePos, bottom, side, front, -(::sDrawWidthX + ::sDrawWidthShortOffsetX), -(::sDrawWidthZ + ::sDrawWidthShortOffsetZ),
                  ::sDrawWidthX, (::sDrawWidthZ + ::sDrawWidthShortOffsetZ), ::sColorMinusX, ::sColorPlusZ, 0.0f, texY);

    ::drawPolygon(rDraw, mBasePos, bottom, side, front, ::sDrawWidthX + ::sDrawWidthLongOffsetX, -::sDrawWidthShortOffsetZ,
                  -(::sDrawWidthX + ::sDrawWidthLongOffsetX), -::sDrawWidthShortOffsetZ, ::sColorPlusX, ::sColorMinusX, 0.0f, texY);
}

void SwingRope::drawFree(TypedDraw& rDraw) const {
    ::drawLine(rDraw, this, ::sColorPlusZ, ::sColorPlusX, -::sDrawWidthX, (::sDrawWidthZ + ::sDrawWidthShortOffsetZ),
               ::sDrawWidthX + ::sDrawWidthShortOffsetX, -(::sDrawWidthZ + ::sDrawWidthShortOffsetZ));
    ::drawLine(rDraw, this, ::sColorMinusX, ::sColorPlusZ, -(::sDrawWidthX + ::sDrawWidthShortOffsetX), -(::sDrawWidthZ + ::sDrawWidthShortOffsetZ),
               ::sDrawWidthX, (::sDrawWidthZ + ::sDrawWidthShortOffsetZ));
    ::drawLine(rDraw, this, ::sColorMinusX, ::sColorPlusZ, ::sDrawWidthX + ::sDrawWidthLongOffsetX, -::sDrawWidthShortOffsetZ,
               -(::sDrawWidthX + ::sDrawWidthLongOffsetX), -::sDrawWidthShortOffsetZ);
}

SwingRopeGroup::SwingRopeGroup(const char* pName) : NameObj(pName) {
    mTexture = new GameTexture(MR::loadTexFromArc("SwingRope", "SwingRope.bti"));

    mMaterial = makeMaterial();
}

SiteMaterial SwingRopeGroup::makeMaterial() const {
    // runtime/src/render/materials/tex_modulate.hlsl: the texture times the vertex colour, its alpha the texture's; drawn where
    // the alpha is above 0, depth tested and written, no culling.
    SiteMaterial material;
    material.init(NEBULA_SHADER_TEX_MODULATE, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, true, NEBULA_CULL_NONE);
    material.constants[0] = 1.0f;
    material.constants[1] = 0.0f;
    material.setTexture(0, mTexture);
    return material;
}
