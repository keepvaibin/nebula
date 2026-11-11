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
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_cancelSquatMode_f1, "cancelSquatMode.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_getTargetWalkSpeed_f1, "getTargetWalkSpeed.f1", 256.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideSquatWalkAnimation_f1, "decideSquatWalkAnimation.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideSquatWalkAnimation_f2, "decideSquatWalkAnimation.f2", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideSquatWalkAnimation_f3, "decideSquatWalkAnimation.f3", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideSquatWalkAnimation_f4, "decideSquatWalkAnimation.f4", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideSquatWalkAnimation_f5, "decideSquatWalkAnimation.f5", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideSquatWalkAnimation_f6, "decideSquatWalkAnimation.f6", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideSquatWalkAnimation_f7, "decideSquatWalkAnimation.f7", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideSquatWalkAnimation_f8, "decideSquatWalkAnimation.f8", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideSquatWalkAnimation_f9, "decideSquatWalkAnimation.f9", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideSquatWalkAnimation_f10, "decideSquatWalkAnimation.f10", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideSquatWalkAnimation_f11, "decideSquatWalkAnimation.f11", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideSquatWalkAnimation_f12, "decideSquatWalkAnimation.f12", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkSpeed_f1, "decideWalkSpeed.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkSpeed_f2, "decideWalkSpeed.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f1, "decideWalkAnimation.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f2, "decideWalkAnimation.f2", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f3, "decideWalkAnimation.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f4, "decideWalkAnimation.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f5, "decideWalkAnimation.f5", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f6, "decideWalkAnimation.f6", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f7, "decideWalkAnimation.f7", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f8, "decideWalkAnimation.f8", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f9, "decideWalkAnimation.f9", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f10, "decideWalkAnimation.f10", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f11, "decideWalkAnimation.f11", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f12, "decideWalkAnimation.f12", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f13, "decideWalkAnimation.f13", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f14, "decideWalkAnimation.f14", 0.33f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f15, "decideWalkAnimation.f15", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f16, "decideWalkAnimation.f16", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f17, "decideWalkAnimation.f17", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f18, "decideWalkAnimation.f18", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f19, "decideWalkAnimation.f19", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f20, "decideWalkAnimation.f20", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f21, "decideWalkAnimation.f21", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f22, "decideWalkAnimation.f22", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f23, "decideWalkAnimation.f23", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f24, "decideWalkAnimation.f24", 1.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f25, "decideWalkAnimation.f25", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f26, "decideWalkAnimation.f26", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f27, "decideWalkAnimation.f27", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f28, "decideWalkAnimation.f28", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f29, "decideWalkAnimation.f29", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideWalkAnimation_f30, "decideWalkAnimation.f30", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_doBrakingAnimation_f1, "doBrakingAnimation.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateWalkSpeed_f1, "updateWalkSpeed.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateWalkSpeed_f2, "updateWalkSpeed.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateWalkSpeed_f3, "updateWalkSpeed.f3", 0.9999f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateWalkSpeed_f4, "updateWalkSpeed.f4", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateWalkSpeed_f5, "updateWalkSpeed.f5", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateWalkSpeed_f6, "updateWalkSpeed.f6", 256.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideOnIceAnimation_f1, "decideOnIceAnimation.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideOnIceAnimation_f2, "decideOnIceAnimation.f2", 0.7f);
NEBULA_TUNABLE_NAMED(f32, cNum_decideOnIceAnimation_f3, "decideOnIceAnimation.f3", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnSand_f1, "updateOnSand.f1", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnSand_f2, "updateOnSand.f2", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnWater_f1, "updateOnWater.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnWater_f2, "updateOnWater.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnWater_f3, "updateOnWater.f3", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnWater_f4, "updateOnWater.f4", 3.0f);

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
            if (mWalkSpeed > cNum_cancelSquatMode_f1) {
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
            targetWalkSpeed *= 1.0f - static_cast< f32 >(mSinkTimer) / cNum_getTargetWalkSpeed_f1;
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
        if (mWalkSpeed > cNum_decideSquatWalkAnimation_f1) {
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
        animspeed = cNum_decideSquatWalkAnimation_f2 / animspeed * mWalkSpeed;
        if (!mMovementStates.squatWalk) {
            animspeed = 1.0f;
        }

        getAnimator()->getXanimePlayer()->changeSpeed(animspeed);
    }

    if (getFloorCode() == 32 && mTargetWalkSpeedIndex != 0) {
        mSpinInertiaBlend = cNum_decideSquatWalkAnimation_f3;
    }

    f32 animFrame = getAnimator()->getFrame();

    if (mTargetWalkSpeedIndex != 0) {
        if (mDrawStates.inWater || mDrawStates.inShallowWater) {
            if (mPrevAnimFrame > animFrame) {
                playSound("水跳ね左足小");
                playEffect("水はね左弱");
                playEffectSRT("水波紋", cNum_decideSquatWalkAnimation_f4, mRippleRotate, (mGroundPos - mSideVec * cNum_decideSquatWalkAnimation_f5) + mGroundNorm * mRippleHeight);
            }

            if (mPrevAnimFrame < cNum_decideSquatWalkAnimation_f6 && animFrame >= cNum_decideSquatWalkAnimation_f7) {
                playSound("水跳ね右足小");
                playEffect("水はね右弱");
                playEffectSRT("水波紋", cNum_decideSquatWalkAnimation_f8, mRippleRotate, (mGroundPos + mSideVec * cNum_decideSquatWalkAnimation_f9) + mGroundNorm * mRippleHeight);
            }
        }
    } else if (mDrawStates.inWater && mActor->mFrameCount % 64 == 0) {
        playEffectSRT("水波紋", cNum_decideSquatWalkAnimation_f10, mRippleRotate, (mShadowPos + mSideVec * (MR::getRandom() - cNum_decideSquatWalkAnimation_f11) * cNum_decideSquatWalkAnimation_f12) + mGroundNorm * mRippleHeight);
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
            getAnimator()->getXanimePlayer()->mPoseBlendRatio = cNum_decideWalkSpeed_f1;
        } else {
            getAnimator()->getXanimePlayer()->mPoseBlendRatio = 1.0f;
        }
    } else {
        f32 speed = getAnimator()->getXanimePlayer()->mPoseBlendRatio;
        f32 new0C = speed;
        new0C += cNum_decideWalkSpeed_f2;

        if (new0C > 1.0f) {
            new0C = 1.0f;
        }

        getAnimator()->getXanimePlayer()->mPoseBlendRatio = new0C;
    }
}

void Mario::decideWalkAnimation() {
    if (mTargetWalkSpeedIndex == 0 && mWalkSpeed < cNum_decideWalkAnimation_f1 && isBlendWaitGround()) {
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
                        sinkBlend = cNum_decideWalkAnimation_f2;
                    }

                    if (mTargetWalkSpeedIndex == 3) {
                        sinkBlend = cNum_decideWalkAnimation_f3;
                    }

                    weights[0] = sinkBlend;
                    weights[1] = 1.0f - sinkBlend;
                } else {
                    if (mTargetWalkSpeedIndex >= 5) {
                        sinkBlend = static_cast< f32 >(mSinkTimer) / cNum_decideWalkAnimation_f4;
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

                playEffectSRT("水波紋", cNum_decideWalkAnimation_f5, mRippleRotate, (mGroundPos - mSideVec * cNum_decideWalkAnimation_f6) + mGroundNorm * mRippleHeight);
            }

            if (mPrevAnimFrame < cNum_decideWalkAnimation_f7 && animFrame >= cNum_decideWalkAnimation_f8) {
                if (mTargetWalkSpeedIndex >= 2) {
                    if (mTargetWalkSpeedIndex < 6) {
                        playSound("水跳ね右足小");
                        playEffect("水はね右弱");
                    } else {
                        playSound("水跳ね右足");
                        playEffect("水はね右");
                    }
                }

                playEffectSRT("水波紋", cNum_decideWalkAnimation_f9, mRippleRotate, (mGroundPos + mSideVec * cNum_decideWalkAnimation_f10) + mGroundNorm * mRippleHeight);
            }
        }
    } else if (mDrawStates.inWater && mActor->mFrameCount % 64 == 0) {
        playEffectSRT("水波紋", cNum_decideWalkAnimation_f11, mRippleRotate, (mShadowPos + mSideVec * (MR::getRandom() - cNum_decideWalkAnimation_f12) * cNum_decideWalkAnimation_f13) + mGroundNorm * mRippleHeight);
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
        animationSpeed = cNum_decideWalkAnimation_f14;
    } else {
        animationSpeed = cNum_decideWalkAnimation_f15 * (cNum_decideWalkAnimation_f16 * ((cNum_decideWalkAnimation_f17 * (mWalkSpeed * mActor->getConst().getTable()->mWalkSpeed)) / stepLength));
    }

    if (mSlipPower > 0.0f && !mDrawStates.slopeRunUp) {
        animationSpeed *= 1.0f + ((mSlipPower / cNum_decideWalkAnimation_f18) * mActor->getConst().getTable()->mSlopeAnimeRatio);

        if (mSlipPower > cNum_decideWalkAnimation_f19 && animationSpeed > cNum_decideWalkAnimation_f20) {
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
    f32 spinInertia = 1.0f + cNum_decideWalkAnimation_f21 * diffFromTargetSpeed;

    if (spinInertia > cNum_decideWalkAnimation_f22) {
        spinInertia = cNum_decideWalkAnimation_f23;
    }

    if (spinInertia < 1.0f) {
        spinInertia = 1.0f;
    }

    if (getTargetWalkSpeed() < ::sWalkTargetTable[6]) {
        spinInertia = 1.0f;
    }

    if (getFloorCode() == 32 && mTargetWalkSpeedIndex != 0) {
        spinInertia = cNum_decideWalkAnimation_f24;
    }

    f32 spinStartSpeed = spinStartRatio * (cNum_decideWalkAnimation_f25 * (cNum_decideWalkAnimation_f26 * ((getTargetWalkSpeed() * mActor->getConst().getTable()->mWalkSpeed * cNum_decideWalkAnimation_f27) / footStep[2])));
    f32 factor2 = cNum_decideWalkAnimation_f28 - spinInertia;

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

    f32 runSpeedMin = cNum_decideWalkAnimation_f29;

    if (gIsLuigi) {
        runSpeedMin = cNum_decideWalkAnimation_f30;
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
        getAnimator()->getXanimePlayer()->changeSpeed(cNum_doBrakingAnimation_f1);
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
            if (MR::vecKillElement(mRelativeMove, *getGravityVec(), &result) < -cNum_updateWalkSpeed_f1) {
                press = true;
            } else {
                Vec3f collisionTrans;
                Vec3f collisionPrevTrans;
                MR::extractMtxTrans(mCeilSensor->mHost->mCollisionParts->mBaseMatrix, &collisionTrans);
                MR::extractMtxTrans(mCeilSensor->mHost->mCollisionParts->mPrevBaseMatrix, &collisionPrevTrans);
                if (MR::vecKillElement(collisionTrans - collisionPrevTrans, *getGravityVec(), &result) < -cNum_updateWalkSpeed_f2) {
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
        if (mWalkSpeed >= cNum_updateWalkSpeed_f3) {
            targetWalkSpeed *= getActor()->getConst().getTable()->mDashMultiply;
            if (targetWalkSpeed > mWalkSpeed) {
                inertia = cNum_updateWalkSpeed_f4;
            }

            if (getPlayer()->mWalkSpeed >= cNum_updateWalkSpeed_f5) {
                getAnimator()->getXanimePlayer()->changeTrackAnimation(2, "メタルダッシュ");
            }
        } else {
            getAnimator()->stopWaitAnimation();
        }
    }

    targetWalkSpeed = targetWalkSpeed * static_cast< f32 >(256 - mSinkTimer) / cNum_updateWalkSpeed_f6;
    mWalkSpeed = mWalkSpeed * inertia + targetWalkSpeed * (1.0f - inertia);
}

void Mario::decideOnIceAnimation() {
    if (mTargetWalkSpeedIndex == 0) {
        if (mWalkSpeed > cNum_decideOnIceAnimation_f1 && !isAnimationRun("氷上慣性走行")) {
            changeAnimationWithAttr("氷上慣性走行", 1);
            mIceAnimFoot = 1 - mIceAnimFoot;
        }
    } else {
        decideWalkAnimation();
        if (mWalkSpeed > cNum_decideOnIceAnimation_f2) {
            switch (mIceAnimFoot) {
            case 0:
                getAnimator()->getXanimePlayer()->changeTrackAnimation(2, "氷上力行左");
                break;
            default:
                getAnimator()->getXanimePlayer()->changeTrackAnimation(2, "氷上力行右");
            }
        }
    }

    if (mTargetWalkSpeedIndex != 0 || mWalkSpeed <= cNum_decideOnIceAnimation_f3) {
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

                    if (getAirGravityVec().dot(mGroundNorm) > -cNum_updateOnSand_f1) {
                        Vec3f vec1;
                        MR::vecKillElement(mGroundNorm, getAirGravityVec(), &vec1);
                        Vec3f vec2;
                        vec2.cross(vec1, mGroundNorm);
                        MR::normalize(&vec2);
                        vec1.cross(mGroundNorm, vec2);
                        addVelocity(vec1 * cNum_updateOnSand_f2);
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
            mRippleHeight = cNum_updateOnWater_f1;
            mRippleRotate = mGroundNorm;

            switch (mPolygonFloorCode) {
            case 20:
                mRippleHeight += cNum_updateOnWater_f2;
            case 21:
                mRippleHeight += cNum_updateOnWater_f3;
            case 22:
                mDrawStates.inWater = true;
            }
        }

        if (mFloorCode == 23 && mPolygonFloorCode == 23) {
            touchWater();
            mDrawStates.inShallowWater = true;
            mRippleHeight = cNum_updateOnWater_f4;
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
