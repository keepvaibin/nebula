#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioSearchLight.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_trySetLockOnTarget_f1, "trySetLockOnTarget.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPullTrans_f1, "tryPullTrans.f1", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPullTrans_f2, "tryPullTrans.f2", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPullTrans_f3, "tryPullTrans.f3", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPullTrans_f4, "tryPullTrans.f4", 400000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPullTrans_f5, "tryPullTrans.f5", 10000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPullTrans_f6, "tryPullTrans.f6", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPullTrans_f7, "tryPullTrans.f7", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPullTrans_f8, "tryPullTrans.f8", 0.04f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPullTrans_f9, "tryPullTrans.f9", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPullTrans_f10, "tryPullTrans.f10", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPullTrans_f11, "tryPullTrans.f11", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPullTrans_f12, "tryPullTrans.f12", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryPullTrans_f13, "tryPullTrans.f13", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_releaseThrowMemoSensor_f1, "releaseThrowMemoSensor.f1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_releaseThrowMemoSensor_f2, "releaseThrowMemoSensor.f2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryTornadoPull_f1, "tryTornadoPull.f1", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryTornadoPull_f2, "tryTornadoPull.f2", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryTornadoPull_f3, "tryTornadoPull.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryTornadoPull_f4, "tryTornadoPull.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryTornadoPull_f5, "tryTornadoPull.f5", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryTornadoPull_f6, "tryTornadoPull.f6", 120.0f);

void MarioActor::memorizeSensorThrow(HitSensor* pSensor) {
    mTakeSensors.at(mTakeCount) = pSensor;
    mTakeRotations.at(mTakeCount) = pSensor->mHost->mRotation - mRotation;

    mTakenSensor = nullptr;
    mTakeCount++;
    mTakingWobble = 0.0f;
    mSwingInhibitTimer = 30;

    mMario->startHandy();

    if (pSensor == mThrowTarget) {
        mThrowTarget = nullptr;
    }

    if (pSensor == mTakeCandidate) {
        mTakeCandidate = nullptr;
    }
}

bool MarioActor::tryThrow() {
    if (mTakeCount == 0) {
        return false;
    }

    if (IsMarioSwimming()) {
        return false;
    }

    if (mMario->isStatusActive(MarioStatus_Bury)) {
        return false;
    }

    if (!isRequestThrow()) {
        return false;
    }

    HitSensor* pSensor = mTakeSensors[mTakeCount - 1];

    if (releaseThrowMemoSensor()) {
        mMario->mSwim->resetJet();
        mMarioAnim->changeThrowAnimation(pSensor);

        mMainSpinBlock = 30;
        mVelocity.zero();

        if (isJumping()) {
            mStunTimer = 1;
            jumpHop();
        } else {
            mStunTimer = 15;
        }

        offTakingFlag();

        return true;
    }

    return false;
}

void MarioActor::tryReleaseDirect(const HitSensor* pSensor) {
    u32 maxIdx = mTakeCount;
    for (s32 idx = 0; idx < maxIdx; idx++) {
        if (mTakeSensors[idx] == pSensor) {
            mTakeSensors[idx] = nullptr;
            mTakeCount--;

            continue;
        }

        if (idx == 0) {
            continue;
        }

        for (u32 innerIdx = 0; innerIdx < idx; innerIdx++) {
            if (mTakeSensors[innerIdx] == nullptr) {
                mTakeSensors[innerIdx] = mTakeSensors[idx];
                mTakeSensors[idx] = nullptr;

                break;
            }
        }
    }

    if (mThrowMemoSensor == pSensor) {
        mThrowMemoSensor = nullptr;
    }

    if (mTakeCount == 0) {
        mMario->stopAnimationUpper(nullptr);
        mMario->mSwim->resetJet();

        offTakingFlag();

        clearNullAnimation(0);
    }
}

void MarioActor::damageDropThrowMemoSensor() {
    tryReleaseWithMsg(ACTMES_DAMAGEDROP);
    updateSearchLight();
    tryReleaseBombTeresa();

    if (mTakenSensor != nullptr) {
        mTakenSensor->receiveMessage(ACTMES_DAMAGEDROP, getSensor("body"));
        mTakenSensor = nullptr;
    }
}

void MarioActor::rushDropThrowMemoSensor() {
    tryReleaseWithMsg(ACTMES_RUSHDROP);
    updateSearchLight();

    if (mTakenSensor != nullptr) {
        mTakenSensor->receiveMessage(ACTMES_DAMAGEDROP, getSensor("body"));
        mTakenSensor = nullptr;
    }
}

void MarioActor::trySetLockOnTarget(HitSensor* pSensor) {
    HitSensor* sensor = mTakeCount == 0 ? nullptr : mTakeSensors[0];

    if (sensor == pSensor) {
        return;
    }

    if (isJumping() && mMario->mJumpKind == 5) {
        return;
    }

    Vec3f vec = pSensor->mPosition - mPosition;
    if (vec.length() > cNum_trySetLockOnTarget_f1) {
        return;
    }

    MR::vecKillElement(vec, getGravityVec(), &vec);

    if (MR::normalizeOrZero(&vec)) {
        return;
    }

    Vec3f front;
    getFrontVec(&front);
    MR::vecKillElement(front, getGravityVec(), &front);

    if (MR::normalizeOrZero(&front)) {
        return;
    }

    f32 diffAngle = MR::diffAngleAbsFast(vec, front) * _180_PI;
    if (mTakeCandidate == nullptr || diffAngle < mLockOnAngle) {
        mTakeCandidate = pSensor;
        mLockOnAngle = diffAngle;
    }
}

void MarioActor::tryCoinPull() {
    for (s32 idx = 0; idx < mPullSensors.size(); idx++) {
        if (mPullSensors[idx] == nullptr) {
            continue;
        }

        if (MR::isDead(mPullSensors[idx]->mHost)) {
            mPullSensors[idx] = nullptr;
        } else {
            tryCoinPullOne(mPullSensors[idx]);
        }
    }
}

void MarioActor::tryCoinPullInRush() {
    for (u32 idx = 0; idx < mClapCoinCount; idx++) {
        if (mClapCoins[idx] == nullptr) {
            continue;
        }

        if (MR::isDead(mClapCoins[idx]->mHost)) {
            mClapCoins[idx] = nullptr;
        } else {
            tryTornadoPull(mClapCoins[idx]);
        }
    }
}

void MarioActor::flushCoinPull() {
    for (s32 idx = 0; idx < mPullSensors.size(); idx++) {
        if (mPullSensors[idx] == nullptr) {
            continue;
        }

        if (MR::isDead(mPullSensors[idx]->mHost)) {
            mPullSensors[idx] = nullptr;
        } else {
            tryGetItem(mPullSensors[idx]);
            mPullSensors[idx] = nullptr;
        }
    }
}

bool MarioActor::tryCoinPullOne(HitSensor* pSensor) {
    tryPullTrans(&pSensor->mHost->mVelocity, pSensor->mPosition);

    return pSensor->receiveMessage(ACTMES_ITEM_PULL, getSensor("body"));
}

void MarioActor::tryPullTrans(Vec3f* pVelocity, const Vec3f& rPosition) {
    Vec3f center(mCenterPos);
    Vec3f ground;
    getGroundPos(&ground);
    if (IsMarioSwimming()) {
        ground = center - mBodyUp * cNum_tryPullTrans_f1;
    } else if (mMario->mMovementStates.onGround) {
        Vec3f offset = ground - center;
        f32 height = MR::vecKillElement(offset, mMario->mGroundNorm, &offset);
        ground = center + mMario->mGroundNorm * height;
    }

    Vec3f foot;
    f32 projection = MR::getFootPoint(center, ground, rPosition, &foot);
    Vec3f inward = foot - rPosition;
    f32 radius = inward.length();
    if (radius > cNum_tryPullTrans_f2) {
        radius = cNum_tryPullTrans_f3;
    }

    Vec3f speed;
    speed.y = cNum_tryPullTrans_f4 / (cNum_tryPullTrans_f5 + radius * radius);
    f32 height = MR::abs(projection * (ground - center).length());
    if (height < speed.y) {
        speed.y = height;
    }

    if (projection < 0.0f) {
        speed.y = -speed.y;
    }

    if (!IsMarioSwimming()) {
        speed.y = cNum_tryPullTrans_f6 * speed.y;
    }

    Vec3f axis = ground - center;
    MR::normalizeOrZero(&axis);
    MR::normalizeOrZero(&inward);
    Vec3f tangent;
    tangent.cross(axis, inward);
    speed.z = (cNum_tryPullTrans_f7 + cNum_tryPullTrans_f8 * (cNum_tryPullTrans_f9 - radius) / cNum_tryPullTrans_f10) * (cNum_tryPullTrans_f11 * (MR::pi() * radius)) * getConst().getTable()->mCoinPullAngleSpeedRatio;
    Vec3f nextPosition = rPosition + tangent * speed.z;
    Vec3f pull = foot - nextPosition;
    f32 nextRadius = pull.length();
    MR::normalizeOrZero(&pull);
    f32 pullSpeed = radius / cNum_tryPullTrans_f12;
    if (mInRush) {
        pullSpeed += cNum_tryPullTrans_f13;
    }

    pullSpeed *= getConst().getTable()->mCoinPullDistSpeedRatio;
    if (radius < pullSpeed) {
        pullSpeed = radius;
    }

    speed.x = pullSpeed + (nextRadius - radius);
    *pVelocity = pull * speed.x - axis * speed.y + tangent * speed.z;
}

bool MarioActor::releaseThrowMemoSensor() {
    if (mTakeCount != 0) {
        if (mMario->isStatusActive(MarioStatus_Wall) || mMario->isStatusActive(MarioStatus_SideStep)) {
            return false;
        }

        if (mMario->isSwimming() && mSearchLight->mTarget != nullptr) {
            mThrowTarget = mSearchLight->mTarget;
        }

        s32 idx = mTakeCount - 1;
        if (mThrowTarget != nullptr) {
            Vec3f diff = mThrowTarget->mPosition - mCenterPos;

            if (mMario->isSwimming()) {
                Vec3f throwVec;
                getThrowVec(&throwVec);
                if (MR::diffAngleAbs(diff, throwVec) > PI / cNum_releaseThrowMemoSensor_f1) {
                    mThrowTarget = nullptr;
                }
            } else if (MR::diffAngleAbs(diff, mMario->mFrontVec) > PI / cNum_releaseThrowMemoSensor_f2) {
                mThrowTarget = nullptr;
            }
        }

        if (mThrowTarget == nullptr || !mMario->isSwimming()) {
            if (mThrowAimSensor != nullptr) {
                if (!mTakeSensors[idx]->receiveMessage(ACTMES_THROW, mThrowAimSensor)) {
                    return false;
                }
            } else if (!mTakeSensors[idx]->receiveMessage(ACTMES_THROW, getSensor("body"))) {
                return false;
            }

            mThrowMemoSensor = 0;
        } else {
            if (!mTakeSensors[idx]->receiveMessage(ACTMES_THROW, mThrowTarget)) {
                return false;
            }

            mThrowMemoSensor = mThrowTarget;
        }

        mTakeCount--;

        if (mTakeCount == 0) {
            mThrowTarget = nullptr;
            mThrowVecTimer = 0;
            mTakeSensors[0] = nullptr;
        }

        clearNullAnimation(0);

        mItemPickupBlock = 60;

        return true;
    }

    return false;
}

void MarioActor::tryReleaseWithMsg(u32 msg) {
    u32 maxIdx = mTakeCount;
    if (maxIdx == 0) {
        if (isAnimationRun("ひろいクイック")) {
            mMario->stopAnimationUpperForce();
            clearNullAnimation(0);
            offTakingFlag();
        }

        return;
    }

    for (s32 idx = 0; idx < maxIdx; idx++) {
        if (!mTakeSensors[idx]->receiveMessage(msg, getSensor("body"))) {
            if (idx == 0) {
                continue;
            }

            for (u32 innerIdx = 0; innerIdx < idx; innerIdx++) {
                if (mTakeSensors[innerIdx] == nullptr) {
                    mTakeSensors[innerIdx] = mTakeSensors[idx];
                    mTakeSensors[idx] = nullptr;

                    break;
                }
            }
        } else {
            mTakeSensors[idx] = nullptr;
            mTakeCount--;
        }
    }

    mThrowMemoSensor = nullptr;

    if (mTakeCount == 0) {
        mMario->stopAnimationUpperForce();
        mMario->mSwim->resetJet();
    }

    if (mTaking) {
        mMario->stopAnimationUpperForce();
        offTakingFlag();
    }

    clearNullAnimation(0);
}

void MarioActor::tryTornadoPull(HitSensor* pSensor) {
    if (!isActionOk("コイン引っ張り")) {
        return;
    }

    s32 type = pSensor->mType;
    if (MR::isEqualString(pSensor->mHost->mName, "カメックビーム用カメ")) {
        type = 5555;
    }

    switch (type) {
    case ATYPE_COIN_RED:
        break;
    case ATYPE_COIN:
    case ATYPE_STAR_PIECE: {
        if (mPlayerMode == 7 || IsMarioSwimming()) {
            Vec3f center(mCenterPos);
            Vec3f ground;
            getGroundPos(&ground);
            if (IsMarioSwimming()) {
                ground = center - mBodyUp * cNum_tryTornadoPull_f1;
            }

            center += mMario->mHeadVec * cNum_tryTornadoPull_f2;
            Vec3f position(pSensor->mPosition);
            Vec3f foot;
            f32 projection = MR::getFootPoint(center, ground, position, &foot);
            if (projection < -cNum_tryTornadoPull_f3) {
                return;
            }

            if (projection > cNum_tryTornadoPull_f4) {
                return;
            }

            Vec3f offset = foot - position;
            if (offset.length() > cNum_tryTornadoPull_f5) {
                return;
            }

            if (MR::isExistMapCollision(mCenterPos, pSensor->mPosition - mCenterPos)) {
                return;
            }

            for (s32 i = 0; i < mPullSensors.size(); i++) {
                if (mPullSensors[i] == pSensor) {
                    return;
                }
            }

            if (pSensor->receiveMessage(ACTMES_ITEM_PULL, getSensor("body"))) {
                s32 i = 0;
                while (i < mPullSensors.size() && mPullSensors[i] != nullptr) {
                    i++;
                }

                mPullSensors.at(i) = pSensor;
            }
        }

        break;
    }

    case ATYPE_JET_TURTLE:
    case ATYPE_JET_TURTLE_SLOW:
        if (isActionOk("カメ持ち") && pSensor->receiveMessage(ACTMES_IS_PULL_ENABLE, getSensor("body"))) {
            if ((pSensor->mPosition - mPosition).length() < cNum_tryTornadoPull_f6) {
                tryGetItem(pSensor);
                return;
            }

            tryCoinPullOne(pSensor);
            mTakenSensor = pSensor;
        }

        break;
    case 5555:
        if (isActionOk("カメ持ち") && pSensor->receiveMessage(ACTMES_IS_PULL_ENABLE, getSensor("body"))) {
            tryCoinPullOne(pSensor);
        }

        break;
    }
}

void MarioActor::tryReleaseBombTeresa() {
    if (mBombTeresaTimer == 0) {
        return;
    }

    mBombTeresaTimer = 0;

    mMario->stopAnimationUpper(nullptr);

    if (!mMario->getMovementStates().jumping) {
        MarioStopWalk();
        mStunTimer = 5;
        mMario->mMoveLockTimer = 25;
    }

    changeAnimation("ハンマー投げリリース");
}
