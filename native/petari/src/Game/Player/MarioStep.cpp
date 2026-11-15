#include "Game/Player/MarioStep.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <nebula_math.h>

void Mario::checkStep() {
    if (mMovementStates.jumping) {
        return;
    }

    if (mMovementStates.squat) {
        return;
    }

    if (!mMovementStates.onGround) {
        return;
    }

    if (isStatusActive(MarioStatus_Hang)) {
        return;
    }

    if (isStatusActive(MarioStatus_Wall)) {
        return;
    }

    if (isStatusActive(MarioStatus_Swim)) {
        return;
    }

    if (isStatusActive(MarioStatus_Climb)) {
        return;
    }


    if (isDamaging()) {
        return;
    }

    if (isSwimming()) {
        return;
    }

    if (!MR::isNearZero(mRelativeMove)) {
        return;
    }

    if (!isStickOn()) {
        return;
    }

    if (mCorrectionEaseTimer) {
        return;
    }

    if (mDrawStates.onSlipFloor) {
        return;
    }

    if (isSlipPolygon(mShadowPolygon)) {
        return;
    }

    if (mStepPolygon->isValid() && !mMovementStates.frontWall && mMovementStates.onGround) {
        const f32 frontDot = mFrontVec.dot(*mStepPolygon->getNormal(0));
        Vec3f horizontal;
        MR::vecKillElement(mStepHitPos - mPosition, -*getGravityVec(), &horizontal);

        if (horizontal.length() < 20.0f && MR::abs(frontDot) < 0.3926991f) {
            f32 stepHeight = (mStepHitPos - mPosition).dot(-*getGravityVec());
            if (stepHeight < mActor->getConst().getTable()->mWalkStepHeight) {
                startStep(mStepHitPos);
            }
        } else if (MR::abs(frontDot) < 1.0471976f) {
            Mtx rotMtx;
            NebulaMtxRotAxisRad(rotMtx, &mSideVec, frontDot);
            NebulaMtxMultVec(rotMtx, &mVelocity, &mVelocity);
        }

        return;
    }

    if (!mMovementStates.frontLedge) {
        return;
    }

    if (!mMovementStates.frontFloorFound) {
        return;
    }

    const Vec3f* worldPadDir = &getWorldPadDir();
    if (!(worldPadDir->dot(*mFrontWallTriangle->getNormal(0)) < -0.5f)) {
        return;
    }

    f32 stepHeight = (mFrontFloorPos - mPosition).dot(-*getGravityVec());
    if (stepHeight < mActor->getConst().getTable()->mWalkStepHeight) {
        startStep(mFrontFloorPos);
    }
}

void Mario::startStep(const Vec3f& rVec) {
    if (getCurrentStatus() == 0x10) {
        return;
    }

    Vec3f checkPos = rVec - *getGravityVec() * 80.0f;
    HitInfo hitInfo;

    if (MR::checkStrikePointToMap(checkPos, &hitInfo)) {
        return;
    }

    const bool hitNear = Collision::checkStrikeBallToMap(checkPos, 20.0f, nullptr, nullptr);
    if (hitNear) {
        return;
    }

    const bool hitFar = Collision::checkStrikeBallToMap(checkPos, 50.0f, nullptr, nullptr);
    if (hitFar) {
        return;
    }

    Vec3f oldPos(mPosition);
    mPosition = rVec;
    const f32 ceilDist = calcDistToCeil(false);
    mPosition = oldPos;

    if (ceilDist < 200.0f) {
        return;
    }

    if (mCeilPolygon->isValid() && MR::isSensorPressObj(mCeilPolygon->mSensor)) {
        return;
    }

    if (mStepPolygon->isValid()) {
        if (MR::isSensorPressObj(mStepPolygon->mSensor)) {
            return;
        }

        MtxPtr prevBase = mStepPolygon->getPrevBaseMtx()->toMtxPtr();
        MtxPtr base = mStepPolygon->getBaseMtx()->toMtxPtr();

        if (!MR::isSameMtx(base, prevBase) && mStepPolygon->mSensor != mGroundPolygon->mSensor) {
            return;
        }
    }

    setTrans(rVec);
    changeStatus(mStep);
    mTurnSlipTimer = 0;
    const f32 zero = 0.0f;
    mVelocity.z = zero;
    mVelocity.y = zero;
    mVelocity.x = zero;
    mMovementStates.turnStickNeutral = false;
}

MarioStep::MarioStep(MarioActor* pActor) : MarioState(pActor, MarioStatus_Step) {
    mBlendFrames = 0.0f;
    mStartSpeed = 0.0f;
}

bool MarioStep::start() {
    mStartSpeed = getPlayer()->mWalkSpeed;
    MR::clamp(mStartSpeed, 0.0f, 1.0f);

    s32 v2 = 20 - static_cast< s32 >(16.0f * mStartSpeed);

    if (v2 < 4) {
        v2 = 4;
    }

    mActor->setBlendMtxTimer(v2);
    mBlendFrames = static_cast< u16 >(v2);

    if (isAnimationRun(nullptr) && !mActor->mPunchSensorOn) {
        stopAnimation(nullptr);
    }

    return true;
}

bool MarioStep::update() {
    if (mActor->isRequestJump()) {
        getPlayer()->tryJump();
        return false;
    } else if (getPlayer()->mMovementStates.jumping) {
        return false;
    }

    getPlayer()->mainMove();
    clearVelocity();

    f32 walkSpeed = getPlayer()->mWalkSpeed;
    if (walkSpeed < 0.1f) {
        walkSpeed = 0.1f;
    }

    getPlayer()->mWalkSpeed = walkSpeed;
    getPlayer()->updateWalkSpeed();

    if (walkSpeed < 0.5f) {
        walkSpeed = 0.5f;
    }

    f32 framesLeft = mBlendFrames - walkSpeed;
    mBlendFrames -= walkSpeed;

    if (framesLeft < 0.0f) {
        return false;
    } else {
        mActor->setBlendMtxTimer((s32)(framesLeft));
        return true;
    }
}

bool MarioStep::close() {
    return true;
}
