#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_isUseSimpleGroundCheck_f1, "isUseSimpleGroundCheck.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isUseSimpleGroundCheck_f2, "isUseSimpleGroundCheck.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isUseSimpleGroundCheck_f3, "isUseSimpleGroundCheck.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isUseSimpleGroundCheck_f4, "isUseSimpleGroundCheck.f4", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkGroundOnSlope_f1, "checkGroundOnSlope.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkGroundOnSlope_f2, "checkGroundOnSlope.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkGroundOnSlope_f3, "checkGroundOnSlope.f3", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkGroundOnSlope_f4, "checkGroundOnSlope.f4", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkGroundOnSlope_f5, "checkGroundOnSlope.f5", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkGroundOnSlope_f6, "checkGroundOnSlope.f6", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkGroundOnSlope_f7, "checkGroundOnSlope.f7", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkGroundOnSlope_f8, "checkGroundOnSlope.f8", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkGroundOnSlope_f9, "checkGroundOnSlope.f9", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkGroundOnSlope_f10, "checkGroundOnSlope.f10", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkGroundOnSlope_f11, "checkGroundOnSlope.f11", 0.1908f);
NEBULA_TUNABLE_NAMED(f32, cNum_checkGroundOnSlope_f12, "checkGroundOnSlope.f12", 5.0f);

void Mario::lockGroundCheck(void* owner, bool simple) {
    mGroundCheckOwner = owner;
    mActionStates.groundCheckSimple = simple;
    mActionStates.groundCheckLocked = true;
    mMovementStates.simpleGroundCheck = simple;
}

void Mario::unlockGroundCheck(void* owner) {
    if (!owner || mGroundCheckOwner == owner) {
        mGroundCheckOwner = nullptr;
        mActionStates.groundCheckLocked = false;
    }
}

bool Mario::isUseSimpleGroundCheck() const {
    if (mActionStates.groundCheckLocked) {
        return mActionStates.groundCheckSimple;
    }
    if (mMovementStates.slopeSlide) {
        return true;
    }
    if (isPlayerModeTeresa()) {
        return true;
    }
    if (!mMovementStates.onGround) {
        return mMovementStates.slopeSlide;
    }
    if (calcAngleD(mGroundNorm) > mActor->getConst().getTable()->mFlatAngle - cNum_isUseSimpleGroundCheck_f1) {
        Triangle ground;
        if (MR::getFirstPolyOnLineBFast(mPosition - *getGravityVec() * cNum_isUseSimpleGroundCheck_f2, *getGravityVec() * cNum_isUseSimpleGroundCheck_f3, nullptr, &ground)) {
            if (calcAngleD(*ground.getNormal(0)) < mActor->getConst().getTable()->mFlatAngle - cNum_isUseSimpleGroundCheck_f4) {
                return false;
            }
        }
        return true;
    }
    getAnimator()->isLandingAnimationRun();
    return false;
}

bool Mario::checkGroundOnSlope() {
    Vec3f normal;
    if (isAnimationRun("崖ふんばり")) {
        normal = -*getGravityVec();
    } else {
        normal = *mShadowPolygon->getNormal(0);
    }
    if (MR::isNearZero(normal)) {
        return false;
    }
    Vec3f front;
    MR::vecKillElement(mFrontVec, normal, &front);
    if (MR::isNearZero(front)) {
        return false;
    }
    Triangle ground;
    bool hit;
    bool missingGround = false;
    mMovementStates.groundMissing = true;
    f32 distance;
    if (mMovementStates.jumping && isRising()) {
        distance = cNum_checkGroundOnSlope_f1;
    } else if (mLedgeFallTimer) {
        distance = cNum_checkGroundOnSlope_f2;
    } else {
        distance = cNum_checkGroundOnSlope_f3;
    }
    if (mMovementStates.onGround) {
        f32 angle = calcAngleD(mGroundNorm) - cNum_checkGroundOnSlope_f4;
        if (angle < 0.0f) {
            angle = 0.0f;
        }
        if (angle > cNum_checkGroundOnSlope_f5) {
            angle = cNum_checkGroundOnSlope_f6;
        }
        distance += cNum_checkGroundOnSlope_f7 * angle;
    }
    if (getCurrentStatus() == MarioStatus_Slider) {
        distance = cNum_checkGroundOnSlope_f8;
    }
    s32 groundCount = 0;
    Vec3f hitPosition;
    Vec3f positionSum;
    positionSum.zero();
    Vec3f groundPosition;
    Vec3f origin(mPosition - *getGravityVec() * cNum_checkGroundOnSlope_f9);
    hit = MR::getFirstPolyOnLineBFast(origin, *getGravityVec() * distance, &hitPosition, &ground);
    if (hit) {
        if (calcAngleD(*ground.getNormal(0)) >= cNum_checkGroundOnSlope_f10) {
            hit = false;
        }
        f32 gravityDot = getGravityVec()->dot(*ground.getNormal(0));
        if (getCurrentStatus() != MarioStatus_Slider && gravityDot > -cNum_checkGroundOnSlope_f11) {
            hit = false;
        }
        if (hit) {
            positionSum += hitPosition;
            groundCount++;
            setGroundNorm(*ground.getNormal(0));
            *mGroundPolygon = ground;
            groundPosition = hitPosition;
            mGroundPos = groundPosition;
            recordLastGround();
        }
    }
    if (!groundCount) {
        missingGround = true;
    }
    if (mMovementStates.forceGroundOnce) {
        mMovementStates.forceGroundOnce = false;
        Vec3f offset(mGroundPos - mPosition);
        f32 vertical = MR::vecKillElement(offset, *getGravityVec(), &offset);
        addTrans(*getGravityVec() * vertical);
        return true;
    }
    if (mMovementStates.jumping && isRising()) {
        return false;
    }
    if (missingGround) {
        mMovementStates.groundMissing = true;
        return false;
    }
    mMovementStates.groundMissing = false;
    Vec3f down(-mGroundNorm);
    Vec3f position(mGroundPos);
    if (mActionStates.slopePushed) {
        mActionStates.slopePushed = false;
        mHitStates.slopePushed = true;
    } else if ((mMovementStates.slopeSlide || mTargetWalkSpeedIndex) && getPlayerMode() != PlayerMode_Teresa) {
        if (MR::abs((position - mPosition).dot(down)) < distance) {
            Vec3f offset(position - mPosition);
            f32 vertical = MR::vecKillElement(offset, down, &offset);
            if (!MR::isNearZero(vertical, 1.0f)) {
                mDrawStates.snappedToGround = false;
                Vec3f displacement(down * vertical);
                f32 ratio = MR::clamp(mWalkSpeed, 0.0f, 1.0f);
                if (!mMovementStates.slopeSlide) {
                    displacement *= ratio;
                }
                bool notSlipping = !isSlipFloorCode(mFloorCode);
                if (!notSlipping && !mDrawStates.slopeRunUp && !mPrevDrawStates.slopeRunUp) {
                    mVelocityAfter += displacement;
                }
            }
            return true;
        }
    }
    if (mVerticalSpeed < cNum_checkGroundOnSlope_f12) {
        return true;
    }
    if (mDrawStates.binderFloorHit) {
        return true;
    }
    if (groundCount) {
        return true;
    }
    return mDrawStates.snappedToGround;
}
