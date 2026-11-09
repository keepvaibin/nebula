#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioMapCode.hpp"
#include "Game/Player/MarioModule.hpp"
#include "Game/Player/MarioMove.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Util.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>

namespace {
    static f32 sSpeedTableA[] = {0.15f, 0.3f, 0.45f, 0.6f, 0.7f, 0.85f, 0.99f};
    static f32 sSpeedTableB[] = {0.02f, 0.2f, 0.4f, 0.5f, 0.65f, 0.75f, 0.98f};
    static f32 sWalkTargetTable[] = {0.0f, 0.15f, 0.25f, 0.4f, 0.5f, 0.6f, 0.8f, 1.0f};
    static f32 sWeightTable[8][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f},   {0.75f, 0.25f, 0.0f, 0.0f}, {0.25f, 0.75f, 0.0f, 0.0f},
                                     {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.75f, 0.25f, 0.0f}, {0.0f, 0.25f, 0.75f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};
    static f32 sFootStep[] = {1.2f, 1.5f, 1.3f, 0.0f};
    static f32 sFootStepBeeWallWalk[] = {0.5f, 0.5f, 0.5f, 0.0f};
};  // namespace

void Mario::stopWalk() {
    mTargetWalkSpeedIndex = 0;
    mWalkSpeed = 0.0f;
    mBrakeFirstTimer = 0;
    mBrakeTimer = 0;
    mWeakTurnTimer = 0;

    getAnimator()->initWalkWeight();
    getAnimator()->resetTilt();

    mMovementStates.turnStickNeutral = false;
    mMovementStates.slopeSlide = false;
    cancelSquatMode();

    mMovementStates.turning = false;
    mTurnReadyTimer = 0;
    mSlipPower = 0.0f;
    mSpinInertiaBlend = 0.0f;

    stopEffect("共通スリップ坂");
    stopAnimation("歩行制動ブレーキ", MarioIndexedAnime_WalkBrake);
    stopAnimation("ブレーキ");
}

void Mario::cancelSquatMode() {
    if (!mMovementStates.squat) {
        return;
    }

    calcDistToCeil(false);

    if (mPrevMovementStates.squat || mMovementStates.squat) {
        mActionStates.squatEnded = true;
    }

    mMovementStates.squat = false;
    mPrevMovementStates.squat = false;
    stopAnimation("しゃがみ基本");

    if (mMovementStates.onGround || !mMovementStates.jumping) {
        if (!isSwimming()) {
            changeAnimation(nullptr, "基本");
        }

        if (!isAnimationRun("サマーソルト") && mActionStates.squatEnded) {
            if (mWalkSpeed > 0.1f) {
                changeAnimationUpperWeak("しゃがみ終了");
            } else {
                changeAnimation("しゃがみ終了");
            }
        }
    }

    mMovementStates.squatWalk = false;
}

f32 Mario::getTargetWalkSpeed() const {
    if (mMovementStates.squat) {
        return 0.0f;
    }

    f32 targetWalkSpeed = ::sWalkTargetTable[mTargetWalkSpeedIndex];

    if (mSinkTimer != 0) {
        if (checkCurrentFloorCodeSevere(25)) {
            targetWalkSpeed = 0.0f;
        } else if (checkCurrentFloorCodeSevere(31)) {
            targetWalkSpeed = 0.0f;
        } else {
            targetWalkSpeed *= 1.0f - static_cast< f32 >(mSinkTimer) / 256.0f;
        }
    }

    targetWalkSpeed *= 1.0f - mWalkingResist;

    if (mItemDashTimer != 0) {
        targetWalkSpeed *= mActor->getConst().getTable()->mItemDashRatio;
    }

    return targetWalkSpeed;
}

void Mario::decideSquatWalkAnimation() {
    const f32 walkWeights1[] = {1.0f, 0.0f, 0.0f, 0.0f};
    const f32 walkWeights2[] = {0.0f, 1.0f, 0.0f, 0.0f};
    mSpinInertiaBlend = 0.0f;

    if (!mMovementStates.squat) {
        if (mWalkSpeed > 0.1f) {
            stopAnimation("しゃがみ", "基本");
            changeAnimationUpperWeak("しゃがみ終了");
        } else {
            changeAnimation("しゃがみ終了", "基本");
        }

        mMovementStates.squatWalk = false;
        return;
    }

    if (isAnimationRun("壁押し", MarioIndexedAnime_WallPush)) {
        stopAnimation(nullptr);
    }

    if (!mPrevMovementStates.squat || !isAnimationRun("しゃがみ基本")) {
        stopAnimation("歩行制動ブレーキ", MarioIndexedAnime_WalkBrake);
        changeAnimation(nullptr, "しゃがみ基本");
        stopAnimation("飛び込み準備", MarioIndexedAnime_DiveReady);
        getAnimator()->setWalkWeight(&walkWeights1[0]);
        mMovementStates.squatWalk = false;
        if (!mMovementStates.jumping && !isAnimationRun("サマーソルト")) {
            playSound("声しゃがむ");
        }
    }

    if (mWalkSpeed < mActor->getConst().getTable()->mSpeedSquatWalkLower) {
        if (isStickOn()) {
            mWalkSpeed = mActor->getConst().getTable()->mSquatWalkMinSpeed;
            getAnimator()->setWalkWeight(&walkWeights2[0]);
            mTargetWalkSpeedIndex = 1;
            mMovementStates.squatWalk = true;
        } else {
            getAnimator()->setWalkWeight(&walkWeights1[0]);
            mTargetWalkSpeedIndex = 0;
            mMovementStates.squatWalk = false;
        }
    } else if (!isStickOn()) {
        getAnimator()->setWalkWeight(&walkWeights1[0]);
        mTargetWalkSpeedIndex = 0;
    } else if (mMovementStates.squatWalk) {
        MarioConstTable* table = mActor->getConst().getTable();
        mWalkSpeed = (table->mSquatWalkMaxSpeed - table->mSquatWalkMinSpeed) * mStickPos.z + table->mSquatWalkMinSpeed;
        getAnimator()->setWalkWeight(&walkWeights2[0]);
        mTargetWalkSpeedIndex = 1;
    }

    if (isAnimationRun("しゃがみ基本")) {
        f32 animspeed = mActor->getConst().getTable()->mSquatWalkStep;
        animspeed = 60.0f / animspeed * mWalkSpeed;
        if (!mMovementStates.squatWalk) {
            animspeed = 1.0f;
        }

        getAnimator()->getXanimePlayer()->changeSpeed(animspeed);
    }

    if (getFloorCode() == 32 && mTargetWalkSpeedIndex != 0) {
        mSpinInertiaBlend = 0.2f;
    }

    f32 animFrame = getAnimator()->getFrame();

    if (mTargetWalkSpeedIndex != 0) {
        if (mDrawStates.inWater || mDrawStates.inShallowWater) {
            if (mPrevAnimFrame > animFrame) {
                playSound("水跳ね左足小");
                playEffect("水はね左弱");
                playEffectSRT("水波紋", 0.2f, mRippleRotate, (mGroundPos - mSideVec * 20.0f) + mGroundNorm * mRippleHeight);
            }

            if (mPrevAnimFrame < 30.0f && animFrame >= 30.0f) {
                playSound("水跳ね右足小");
                playEffect("水はね右弱");
                playEffectSRT("水波紋", 0.2f, mRippleRotate, (mGroundPos + mSideVec * 20.0f) + mGroundNorm * mRippleHeight);
            }
        }
    } else if (mDrawStates.inWater && mActor->mFrameCount % 64 == 0) {
        playEffectSRT("水波紋", 0.2f, mRippleRotate, (mShadowPos + mSideVec * (MR::getRandom() - 0.5f) * 20.0f) + mGroundNorm * mRippleHeight);
    }

    mPrevAnimFrame = animFrame;
}

void Mario::decideWalkSpeed() {
    u32 i;
    bool canIndexDecrease = false;
    if (mTargetWalkSpeedIndex != 0) {
        f32 threshold = ::sSpeedTableB[mTargetWalkSpeedIndex - 1];
        if (mStickPos.z < threshold) {
            canIndexDecrease = true;
        }
    }

    for (i = 0; i < ARRAY_SIZE(::sSpeedTableA); i++) {
        if (mStickPos.z < ::sSpeedTableA[i]) {
            break;
        }
    }

    if (mTargetWalkSpeedIndex <= i || canIndexDecrease) {
        mTargetWalkSpeedIndex = i;
    }

    s32 clingNum = MR::getKarikariClingNum();
    if (clingNum != 0) {
        if (clingNum > 5) {
            clingNum = 5;
        }

        if (mTargetWalkSpeedIndex > 5 - clingNum) {
            mTargetWalkSpeedIndex = 5 - clingNum;
        }
    }

    if (mDrawStates.inWater && mTargetWalkSpeedIndex > 6) {
        mTargetWalkSpeedIndex = 6;
    }

    if (mActor->mBeeWallWalk != 0 && mTargetWalkSpeedIndex > 4) {
        mTargetWalkSpeedIndex = 4;
    }

    if (mFloorCode == 32) {
        if (mTargetWalkSpeedIndex > 3) {
            mTargetWalkSpeedIndex = 3;
        }

        if (mTargetWalkSpeedIndex != 0) {
            startPadVib(1);
        }

        if (mTargetWalkSpeedIndex > 2) {
            getAnimator()->getXanimePlayer()->mPoseBlendRatio = 0.5f;
        } else {
            getAnimator()->getXanimePlayer()->mPoseBlendRatio = 1.0f;
        }
    } else {
        f32 speed = getAnimator()->getXanimePlayer()->mPoseBlendRatio;
        f32 new0C = speed;
        new0C += 0.1f;

        if (new0C > 1.0f) {
            new0C = 1.0f;
        }

        getAnimator()->getXanimePlayer()->mPoseBlendRatio = new0C;
    }
}

void Mario::decideWalkAnimation() {
    if (mTargetWalkSpeedIndex == 0 && mWalkSpeed < 0.2f && isBlendWaitGround()) {
        getAnimator()->controlWaitAnimation();
    } else {
        getPlayer()->mActionStates.squatEnded = false;

        if (getPlayer()->mTargetWalkSpeedIndex == 0 && mSwim->mDeepWater) {
            bool prepare = !isPlayerModeBee();
            if (prepare) {
                changeAnimation("飛び込み準備", MarioIndexedAnime_DiveReady);
                return;
            }
        }

        if (mSinkTimer == 0) {
            if (getPlayer()->mTargetWalkSpeedIndex != 0) {
                getAnimator()->stopWaitAnimation();
            }

            getAnimator()->setWalkWeight(::sWeightTable[mTargetWalkSpeedIndex]);
        } else {
            f32 weights[] = {0, 0, 0, 0};
            if (mTargetWalkSpeedIndex != 0) {
                f32 sinkBlend = 1.0f;
                if (mTargetWalkSpeedIndex <= 3) {
                    if (mTargetWalkSpeedIndex == 2) {
                        sinkBlend = 0.75f;
                    }

                    if (mTargetWalkSpeedIndex == 3) {
                        sinkBlend = 0.5f;
                    }

                    weights[0] = sinkBlend;
                    weights[1] = 1.0f - sinkBlend;
                } else {
                    if (mTargetWalkSpeedIndex >= 5) {
                        sinkBlend = static_cast< f32 >(mSinkTimer) / 100.0f;
                    }

                    sinkBlend = MR::clamp(sinkBlend, 0.0f, 1.0f);
                    weights[1] = sinkBlend;
                    weights[2] = 1.0f - sinkBlend;
                }
            } else {
                weights[3] = 1.0f;
            }

            getAnimator()->setWalkWeight(&weights[0]);
        }
    }

    stopAnimation("飛び込み準備", MarioIndexedAnime_DiveReady);

    f32 animFrame = getAnimator()->getFrame();
    if (mTargetWalkSpeedIndex != 0) {
        if (mDrawStates.inWater || mDrawStates.inShallowWater) {
            if (mPrevAnimFrame > animFrame) {
                if (mTargetWalkSpeedIndex >= 2) {
                    if (mTargetWalkSpeedIndex < 6) {
                        playSound("水跳ね左足小");
                        playEffect("水はね左弱");
                    } else {
                        playSound("水跳ね左足");
                        playEffect("水はね左");
                    }
                }

                playEffectSRT("水波紋", 0.2f, mRippleRotate, (mGroundPos - mSideVec * 20.0f) + mGroundNorm * mRippleHeight);
            }

            if (mPrevAnimFrame < 30.0f && animFrame >= 30.0f) {
                if (mTargetWalkSpeedIndex >= 2) {
                    if (mTargetWalkSpeedIndex < 6) {
                        playSound("水跳ね右足小");
                        playEffect("水はね右弱");
                    } else {
                        playSound("水跳ね右足");
                        playEffect("水はね右");
                    }
                }

                playEffectSRT("水波紋", 0.2f, mRippleRotate, (mGroundPos + mSideVec * 20.0f) + mGroundNorm * mRippleHeight);
            }
        }
    } else if (mDrawStates.inWater && mActor->mFrameCount % 64 == 0) {
        playEffectSRT("水波紋", 0.2f, mRippleRotate, (mShadowPos + mSideVec * (MR::getRandom() - 0.5f) * 20.0f) + mGroundNorm * mRippleHeight);
    }

    mPrevAnimFrame = animFrame;

    f32* footStep = ::sFootStep;
    f32 stepLength = 0.0f;

    if (mActor->mBeeWallWalk != 0) {
        footStep = ::sFootStepBeeWallWalk;
    }

    for (int i = 0; i < ARRAY_SIZE(*::sWeightTable); i++) {
        stepLength += footStep[i] * ::sWeightTable[mTargetWalkSpeedIndex][i];
    }

    f32 animationSpeed;

    if (stepLength == 0.0f) {
        animationSpeed = 0.33f;
    } else {
        animationSpeed = 0.5f * (60.0f * ((0.01f * (mWalkSpeed * mActor->getConst().getTable()->mWalkSpeed)) / stepLength));
    }

    if (mSlipPower > 0.0f && !mDrawStates.slopeRunUp) {
        animationSpeed *= 1.0f + ((mSlipPower / 10.0f) * mActor->getConst().getTable()->mSlopeAnimeRatio);

        if (mSlipPower > 5.0f && animationSpeed > 4.0f) {
            animationSpeed *= mActor->getConst().getTable()->mSlopeSpinAnimeRatio;
            changeAnimation("がんばり走り");
            startBas("RunSlope", false, 0.0f, 0.0f);
        }
    } else if (mSlopeDashTimer != 0) {
        animationSpeed *= mSlopeDashSpeed;
    }

    f32 diffFromTargetSpeed = getTargetWalkSpeed() - mWalkSpeed;

    if (mWalkSpeed > ::sWalkTargetTable[5]) {
        f32 squared = diffFromTargetSpeed * diffFromTargetSpeed;
        f32 factor = (mWalkSpeed - ::sWalkTargetTable[5]) / (1.0f - ::sWalkTargetTable[5]);
        diffFromTargetSpeed = (diffFromTargetSpeed * (1.0f - factor)) + (squared * factor);
    } else if (mWalkSpeed < ::sWalkTargetTable[3]) {
        f32 sqrt = NebulaSqrt(diffFromTargetSpeed);
        f32 factor = (::sWalkTargetTable[3] - mWalkSpeed) / ::sWalkTargetTable[3];
        diffFromTargetSpeed = (diffFromTargetSpeed * factor) + (sqrt * (1.0f - factor));
    }

    f32 spinStartRatio = mActor->getConst().getTable()->mStartSpinAnimeRatio;
    f32 spinInertia = 1.0f + 4.0f * diffFromTargetSpeed;

    if (spinInertia > 2.0f) {
        spinInertia = 2.0f;
    }

    if (spinInertia < 1.0f) {
        spinInertia = 1.0f;
    }

    if (getTargetWalkSpeed() < ::sWalkTargetTable[6]) {
        spinInertia = 1.0f;
    }

    if (getFloorCode() == 32 && mTargetWalkSpeedIndex != 0) {
        spinInertia = 1.2f;
    }

    f32 spinStartSpeed = spinStartRatio * (0.5f * (60.0f * ((getTargetWalkSpeed() * mActor->getConst().getTable()->mWalkSpeed * 0.01f) / footStep[2])));
    f32 factor2 = 2.0f - spinInertia;

    if (animationSpeed < spinStartSpeed) {
        animationSpeed = animationSpeed * factor2 + spinStartSpeed * (1.0f - factor2);
    }

    if (mSinkTimer != 0 && mTargetWalkSpeedIndex != 0 && animationSpeed < 1.0f) {
        animationSpeed = 1.0f;
    }

    animationSpeed *= (1.0f - mWalkingResist);
    mSpinInertiaBlend = spinInertia - 1.0f;
    getAnimator()->getXanimePlayer()->changeSpeed(animationSpeed);

    if (!mActor->mInDemo && mTargetWalkSpeedIndex == 0 && mActor->mHealth == 1 && mActor->mMaxHealth > 2 &&
        (mRunningBas == nullptr || !mRunningDamageWait)) {
        getAnimator()->getXanimePlayer()->changeTrackAnimation(3, "ダメージウエイト");
        startBas("DamageWait", false, 0.0f, 0.0f);
        mActor->setBlink("DamageWait");
    }

    checkWallPush();

    f32 runSpeedMin = 0.9f;

    if (gIsLuigi) {
        runSpeedMin = 0.3f;
    }

    if (mTargetWalkSpeedIndex > 5 && mWalkSpeed > runSpeedMin && !mDrawStates.slopeRunUp && !mMovementStates.onSkatableFloor) {
        mBrakeFirstTimer = mActor->getConst().getTable()->mBrakeFirstTimer;
    }

    if (mBrakeFirstTimer != 0) {
        mBrakeFirstTimer--;
    }

    if (mTargetWalkSpeedIndex == 0) {
        if (mBrakeFirstTimer != 0 && !isSlipPolygon(mGroundPolygon) && !mDrawStates.homingLockOn) {
            doBrakingAnimation();
            mBrakeTimer = mActor->getConst().getTable()->mBrakeSecondTimer;
        }

        mBrakeFirstTimer = 0;

        s32 clingNum = MR::getKarikariClingNum();
        if (clingNum >= 1) {
            changeAnimationUpper("カリカリ限界");
            stopAnimation("歩行制動ブレーキ", MarioIndexedAnime_WalkBrake);
        }

        if (clingNum < 1 && isAnimationRun("カリカリ限界")) {
            stopAnimationUpper("カリカリ限界");
        }
    }
}

void Mario::doBrakingAnimation() {
    changeAnimation("歩行制動ブレーキ", MarioIndexedAnime_WalkBrake);
    getAnimator()->getXanimePlayer()->mFrameCtrl->mAttribute = 1;
    if (gIsLuigi) {
        getAnimator()->getXanimePlayer()->changeSpeed(0.5f);
    }

    playEffect("共通ブレーキ");
    mBrakeTimer = 0;
}

void Mario::checkWallPush() {
    if (mTargetWalkSpeedIndex != 0 && (mMovementStates.frontWall || mMovementStates.frontWallGlancing) && checkWallJumpCode()) {
        return;
    }

    f32 angle = MR::diffAngleAbsHorizontal(mFrontVec, -getWallNorm(), *getGravityVec());
    bool sideStep = false;
    f32 wallPushAngleRange = getActor()->getConst().getTable()->mWallPushAngleRange;

    bool checkAngle = mTargetWalkSpeedIndex != 0 && mMovementStates.frontWall;

    if (checkAngle && angle < MR::toRadian(wallPushAngleRange)) {
        sideStep = true;
    }

    if (mDrawStates.atEdge) {
        sideStep = false;
    }

    if (mDrawStates.onSlipFloor) {
        sideStep = false;
    }

    if (calcAngleD(getWallNorm()) < getActor()->getConst().getTable()->mForceWallAngle) {
        sideStep = false;
        if (mMovementStates.frontWall && mTargetWalkSpeedIndex != 0) {
            mTargetWalkSpeedIndex = 1;
            mWalkSpeed = 0.0f;
        }
    }

    if (!isAnimationRun("壁押し", MarioIndexedAnime_WallPush) && sideStep) {
        doSideStep();
    }
}

void Mario::updateBrakeAnimation() {
    if (mBrakeTimer != 0) {
        if (!isAnimationRun("歩行制動ブレーキ", MarioIndexedAnime_WalkBrake)) {
            mBrakeTimer = 0;
        } else {
            mBrakeTimer--;
            if (!MR::isNearZero(mStickPos.z)) {
                mBrakeTimer = 0;
            }

            if (mBrakeTimer == 0) {
                stopAnimation(nullptr);
                stopWalk();
            }
        }
    } else if (isAnimationRun("歩行制動ブレーキ", MarioIndexedAnime_WalkBrake) && (isAnimationTerminate(nullptr) || mTargetWalkSpeedIndex != 0)) {
        stopAnimation(nullptr);
    }

    if (!gIsLuigi) {
        return;
    }

    if (!isAnimationRun("歩行制動ブレーキ", MarioIndexedAnime_WalkBrake) && !isAnimationRun("ブレーキ")) {
        return;
    }

    if (mMovementStates.frontWall || mMovementStates.frontWallGlancing) {
        stopAnimation(nullptr);
        mBrakeTimer = 0;
        mBrakeFirstTimer = 0;
        mTurnSlipTimer = 0;
        mTurnReadyTimer = 0;
    } else if (!MR::isDemoActive() && mMovementStates.onGround) {
        playSound("ルイージ滑り");
    }
}

void Mario::updateWalkSpeed() {
    f32 targetWalkSpeed = getTargetWalkSpeed();
    f32 slowStart = 1.0f;

    if (targetWalkSpeed == 0.0f) {
        mSlowStartTimer = getActor()->getConst().getTable()->mSlowStartTime;
    }

    if (mSlowStartTimer != 0) {
        slowStart = getActor()->getConst().getTable()->mSlowStartTime - mSlowStartTimer;
        slowStart /= getActor()->getConst().getTable()->mSlowStartTime;
        mSlowStartTimer--;
    }

    targetWalkSpeed *= slowStart * slowStart;
    if (mMovementStates.tornado || isStatusActive(17)) {
        targetWalkSpeed *= getActor()->getConst().getTable()->mTornadoMultiply;
    }

    bool press = mMovementStates.squat;
    if (mMovementStates.squat && mHitStates.ceilingNear) {
        press = false;
        if (mFloorCodeReader->getCode(mCeilPolygon) == 29) {
            press = true;
        } else if (MR::isSensorPressObj(mCeilSensor)) {
            Vec3f result;
            if (MR::vecKillElement(mRelativeMove, *getGravityVec(), &result) < -0.5f) {
                press = true;
            } else {
                Vec3f collisionTrans;
                Vec3f collisionPrevTrans;
                MR::extractMtxTrans(mCeilSensor->mHost->mCollisionParts->mBaseMatrix, &collisionTrans);
                MR::extractMtxTrans(mCeilSensor->mHost->mCollisionParts->mPrevBaseMatrix, &collisionPrevTrans);
                if (MR::vecKillElement(collisionTrans - collisionPrevTrans, *getGravityVec(), &result) < -0.5f) {
                    press = true;
                }
            }
        }

        if (mMovementStates.onGround && (MR::isEqualSubString(getGroundPolygon()->mSensor->mHost->mName, "TriPod") ||
                                   MR::isEqualSubString(getGroundPolygon()->mSensor->mHost->mName, "Tripod"))) {
            press = false;
        }

        if (mMovementStates.onGround && mCeilSensor == getGroundPolygon()->getSensor()) {
            press = false;
        }

        if (mCeilSensor != nullptr && press) {
            mActor->mPressAxis = mGroundNorm;
            mActor->setPress(0, 0);
        }
    } else {
        mMovementStates.squat = false;

        if (mDashChanceTimer == 0 && mItemDashTimer == 0 && checkSquat(false) && mSinkTimer <= 32 && !isStatusActive(31)) {
            if (!checkLockOnHoming()) {
                mMovementStates.squat = true;
            }

            if (!press && mMovementStates.squat && (mMovementStates.frontWall || mMovementStates.frontWallGlancing)) {
                mTargetWalkSpeedIndex = 0;
                mWalkSpeed = 0.0f;
            }
        }

        if (mHitStates.ceilingNear && !mMovementStates.squat && isAnimationRun("しゃがみ終了")) {
            mMovementStates.squat = true;
        }

        if (!mMovementStates.squat && press) {
            mMovementStates.squat = true;
            cancelSquatMode();
            mBrakeFirstTimer = 0;
        }
    }

    if (mTurnSlipTimer != 0) {
        targetWalkSpeed = 0.0f;
    }

    if (mMovementStates.turnStickNeutral) {
        targetWalkSpeed = 0.0f;
    }

    f32 inertia = decideInertia(targetWalkSpeed);

    if (!mMovementStates.squat && getPlayerMode() == 1) {
        if (mWalkSpeed >= 0.9999f) {
            targetWalkSpeed *= getActor()->getConst().getTable()->mDashMultiply;
            if (targetWalkSpeed > mWalkSpeed) {
                inertia = 0.99f;
            }

            if (getPlayer()->mWalkSpeed >= 1.5f) {
                getAnimator()->getXanimePlayer()->changeTrackAnimation(2, "メタルダッシュ");
            }
        } else {
            getAnimator()->stopWaitAnimation();
        }
    }

    targetWalkSpeed = targetWalkSpeed * static_cast< f32 >(256 - mSinkTimer) / 256.0f;
    mWalkSpeed = mWalkSpeed * inertia + targetWalkSpeed * (1.0f - inertia);
}

void Mario::decideOnIceAnimation() {
    if (mTargetWalkSpeedIndex == 0) {
        if (mWalkSpeed > 0.2f && !isAnimationRun("氷上慣性走行")) {
            changeAnimationWithAttr("氷上慣性走行", 1);
            mIceAnimFoot = 1 - mIceAnimFoot;
        }
    } else {
        decideWalkAnimation();
        if (mWalkSpeed > 0.7f) {
            switch (mIceAnimFoot) {
            case 0:
                getAnimator()->getXanimePlayer()->changeTrackAnimation(2, "氷上力行左");
                break;
            default:
                getAnimator()->getXanimePlayer()->changeTrackAnimation(2, "氷上力行右");
            }
        }
    }

    if (mTargetWalkSpeedIndex != 0 || mWalkSpeed <= 0.2f) {
        stopAnimation("氷上慣性走行");
    }
}

void Mario::updateOnSand() {
    if (mMovementStates.invincible) {
        return;
    }

    if (mMovementStates.onGround) {
        if (mFloorCode == 27 || mFloorCode == 28) {
            if (MR::getSoundCodeIndex(mShadowPolygon) == CollisionSoundCode_Sand && mSinkTimer < 64) {
                mSinkTimer++;
            }
        } else if (isCurrentFloorSink()) {
            if (mSinkTimer < 255) {
                mSinkTimer++;
                if (mFloorCode == 25 || mFloorCode == 31) {
                    if (mFloorCode == 31) {
                        if (mSinkTimer == 1) {
                            playSound("声沼沈み");
                        }

                        playSound("沼強制沈み");
                    } else {
                        if (mSinkTimer == 1) {
                            playSound("声砂沈み");
                        }

                        playSound("砂強制沈み");
                    }

                    stopWalk();
                    mSinkTimer = MR::clamp(static_cast< s32 >(mSinkTimer) + 3, 0, 255);

                    if (getAirGravityVec().dot(mGroundNorm) > -0.99f) {
                        Vec3f vec1;
                        MR::vecKillElement(mGroundNorm, getAirGravityVec(), &vec1);
                        Vec3f vec2;
                        vec2.cross(vec1, mGroundNorm);
                        MR::normalize(&vec2);
                        vec1.cross(mGroundNorm, vec2);
                        addVelocity(vec1 * 6.0f);
                    }
                } else {
                    playSound("砂沈み");
                }
            } else {
                mActor->forceGameOverSink();
                return;
            }

            if (!isAnimationRun(nullptr)) {
                getAnimator()->getXanimePlayer()->changeTrackAnimation(1, "埋まり歩行");
            }
        } else {
            if (mSinkTimer != 0 && !isAnimationRun(nullptr)) {
                getAnimator()->getXanimePlayer()->changeTrackAnimation(1, "歩行");
            }

            mSinkTimer = 0;
        }
    }

    if (mMovementStates.jumping) {
        mSinkTimer = 0;
    }

    if (isStatusActive(6)) {
        mSinkTimer = 0;
    }
}

void Mario::updateOnPoison() {
    if (mMovementStates.onGround) {
        if (checkCurrentFloorCodeSevere(18)) {
            if (mPoisonTimer == 0) {
                mActor->decLife(0);
                playSound("毒沼ダメージ");
                playSound("ダメージ");
                playSound("声小ダメージ");
                if (mActor->mHealth == 0) {
                    mActor->forceGameOver();
                }

                startCamVib(0);
                mActor->mDamageFlashTimer = 1;
            }

            if (mPoisonTimer < 255) {
                mPoisonTimer++;
            } else {
                mPoisonTimer = 0;
            }
        } else {
            mPoisonTimer = 0;
        }
    } else if (mMovementStates.jumping && mAirTimer > 10) {
        mPoisonTimer = 0;
    }

    if (isStatusActive(6)) {
        mPoisonTimer = 0;
    }
}

void Mario::updateOnWater() {
    if (mMovementStates.onGround) {
        switch (mFloorCode) {
        case 20:
        case 21:
        case 22:
            touchWater();
            mRippleHeight = 20.0f;
            mRippleRotate = mGroundNorm;

            switch (mPolygonFloorCode) {
            case 20:
                mRippleHeight += 20.0f;
            case 21:
                mRippleHeight += 20.0f;
            case 22:
                mDrawStates.inWater = true;
            }
        }

        if (mFloorCode == 23 && mPolygonFloorCode == 23) {
            touchWater();
            mDrawStates.inShallowWater = true;
            mRippleHeight = 3.0f;
            mRippleRotate = mGroundNorm;
        }
    }

    switch (mPolygonFloorCode) {
    case 20:
    case 21:
    case 22:
    case 23:
        mDrawStates.onWaterFloor = true;
    }
}
