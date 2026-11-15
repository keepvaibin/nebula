#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"

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
    if (calcAngleD(mGroundNorm) > mActor->getConst().getTable()->mFlatAngle - 5.0f) {
        Triangle ground;
        if (MR::getFirstPolyOnLineBFast(mPosition - *getGravityVec() * 30.0f, *getGravityVec() * 100.0f, nullptr, &ground)) {
            if (calcAngleD(*ground.getNormal(0)) < mActor->getConst().getTable()->mFlatAngle - 5.0f) {
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
        distance = 10.0f;
    } else if (mLedgeFallTimer) {
        distance = 20.0f;
    } else {
        distance = 60.0f;
    }
    if (mMovementStates.onGround) {
        f32 angle = calcAngleD(mGroundNorm) - 30.0f;
        if (angle < 0.0f) {
            angle = 0.0f;
        }
        if (angle > 40.0f) {
            angle = 40.0f;
        }
        distance += 2.0f * angle;
    }
    if (getCurrentStatus() == MarioStatus_Slider) {
        distance = 100.0f;
    }
    s32 groundCount = 0;
    Vec3f hitPosition;
    Vec3f positionSum;
    positionSum.zero();
    Vec3f groundPosition;
    Vec3f origin(mPosition - *getGravityVec() * 30.0f);
    hit = MR::getFirstPolyOnLineBFast(origin, *getGravityVec() * distance, &hitPosition, &ground);
    if (hit) {
        if (calcAngleD(*ground.getNormal(0)) >= 80.0f) {
            hit = false;
        }
        f32 gravityDot = getGravityVec()->dot(*ground.getNormal(0));
        if (getCurrentStatus() != MarioStatus_Slider && gravityDot > -0.1908f) {
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
    if (mVerticalSpeed < 5.0f) {
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
