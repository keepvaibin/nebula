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
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_checkStep_f1, "checkStep.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkStep_f2, "checkStep.f2", 0.3926991f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkStep_f3, "checkStep.f3", 1.0471976f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkStep_f4, "checkStep.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_startStep_f1, "startStep.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_startStep_f2, "startStep.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_startStep_f3, "startStep.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_startStep_f4, "startStep.f4", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_start_f1, "start.f1", 16.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f2, "update.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f3, "update.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f4, "update.f4", 0.5f);

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

        if (horizontal.length() < cNum_checkStep_f1 && MR::abs(frontDot) < cNum_checkStep_f2) {
            f32 stepHeight = (mStepHitPos - mPosition).dot(-*getGravityVec());
            if (stepHeight < mActor->getConst().getTable()->mWalkStepHeight) {
                startStep(mStepHitPos);
            }
        } else if (MR::abs(frontDot) < cNum_checkStep_f3) {
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
    if (!(worldPadDir->dot(*mFrontWallTriangle->getNormal(0)) < -cNum_checkStep_f4)) {
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

    Vec3f checkPos = rVec - *getGravityVec() * cNum_startStep_f1;
    HitInfo hitInfo;

    if (MR::checkStrikePointToMap(checkPos, &hitInfo)) {
        return;
    }

    const bool hitNear = Collision::checkStrikeBallToMap(checkPos, cNum_startStep_f2, nullptr, nullptr);
    if (hitNear) {
        return;
    }

    const bool hitFar = Collision::checkStrikeBallToMap(checkPos, cNum_startStep_f3, nullptr, nullptr);
    if (hitFar) {
        return;
    }

    Vec3f oldPos(mPosition);
    mPosition = rVec;
    const f32 ceilDist = calcDistToCeil(false);
    mPosition = oldPos;

    if (ceilDist < cNum_startStep_f4) {
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

    s32 v2 = 20 - static_cast< s32 >(cNum_start_f1 * mStartSpeed);

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
    if (walkSpeed < cNum_update_f1) {
        walkSpeed = cNum_update_f2;
    }

    getPlayer()->mWalkSpeed = walkSpeed;
    getPlayer()->updateWalkSpeed();

    if (walkSpeed < cNum_update_f3) {
        walkSpeed = cNum_update_f4;
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
