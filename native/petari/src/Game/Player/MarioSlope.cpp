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
    mActionStates._5 = simple;
    mActionStates._4 = true;
    mMovementStates._36 = simple;
}

void Mario::unlockGroundCheck(void* owner) {
    if (!owner || mGroundCheckOwner == owner) {
        mGroundCheckOwner = nullptr;
        mActionStates._4 = false;
    }
}

bool Mario::isUseSimpleGroundCheck() const {
    if (mActionStates._4) {
        return mActionStates._5;
    }
    if (mMovementStates._23) {
        return true;
    }
    if (isPlayerModeTeresa()) {
        return true;
    }
    if (!mMovementStates._1) {
        return mMovementStates._23;
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
    mMovementStates._14 = true;
    f32 distance;
    if (mMovementStates.jumping && isRising()) {
        distance = 10.0f;
    } else if (mLedgeFallTimer) {
        distance = 20.0f;
    } else {
        distance = 60.0f;
    }
    if (mMovementStates._1) {
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
    if (mMovementStates._D) {
        mMovementStates._D = false;
        Vec3f offset(mGroundPos - mPosition);
        f32 vertical = MR::vecKillElement(offset, *getGravityVec(), &offset);
        addTrans(*getGravityVec() * vertical, "force Trans");
        return true;
    }
    if (mMovementStates.jumping && isRising()) {
        return false;
    }
    if (missingGround) {
        mMovementStates._14 = true;
        return false;
    }
    mMovementStates._14 = false;
    Vec3f down(-mGroundNorm);
    Vec3f position(mGroundPos);
    if (mActionStates._22) {
        mActionStates._22 = false;
        mHitStates._C = true;
    } else if ((mMovementStates._23 || mTargetWalkSpeedIndex) && getPlayerMode() != PlayerMode_Teresa) {
        if (MR::abs((position - mPosition).dot(down)) < distance) {
            Vec3f offset(position - mPosition);
            f32 vertical = MR::vecKillElement(offset, down, &offset);
            if (!MR::isNearZero(vertical, 1.0f)) {
                mDrawStates._0 = false;
                Vec3f displacement(down * vertical);
                f32 ratio = MR::clamp(mWalkSpeed, 0.0f, 1.0f);
                if (!mMovementStates._23) {
                    displacement *= ratio;
                }
                bool notSlipping = !isSlipFloorCode(mFloorCode);
                if (!notSlipping && !mDrawStates._4 && !mPrevDrawStates._4) {
                    mVelocityAfter += displacement;
                }
            }
            return true;
        }
    }
    if (mVerticalSpeed < 5.0f) {
        return true;
    }
    if (mDrawStates._6) {
        return true;
    }
    if (groundCount) {
        return true;
    }
    return mDrawStates._0;
}
