#include "Game/Player/MarioSwim.hpp"
#include "Game/AreaObj/WaterArea.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/MapObj/DashRing.hpp"
#include "Game/Player/MarineSnow.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    static f32 cFrontAcc[] = {-0.2f, -0.2f, -0.2f, -0.1f, -0.1f, -0.1f, -0.1f, -0.1f, -0.0f, -0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                              0.0f,  0.0f,  0.0f,  0.2f,  0.2f,  0.2f,  0.3f,  0.3f,  0.4f,  0.5f,  0.5f, 0.5f, 0.5f, 0.5f,
                              0.4f,  0.4f,  0.3f,  0.3f,  0.2f,  0.2f,  0.1f,  0.1f,  0.1f,  0.1f,  0.1f, 0.0f};

    static f32 cFrontAccSpin[] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 8.0f,  6.0f,  4.0f,  2.0f,  1.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,
                                  0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};

    static f32 cFrontAccSpinSurface[] = {0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  8.0f,  6.0f,  4.0f,  2.0f,  1.0f,
                                         -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
    static f32 cWeightTable[] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };

    static f32 cWeightTableSP[] = {0.0f, 0.0f, 0.75f, 0.25f};
    static f32 cLimitAngleSink = FloatUtil::PI() / 1.0001f;
    static f32 cNeutralAngleWait = (FloatUtil::PI() / 2.0f) - (FloatUtil::PI() / 6.0f);
    static f32 cLimitAngleWait = FloatUtil::PI() / 1.5f;
    static f32 cUpperAngleWait = FloatUtil::PI() / 100.0f;
    static f32 cWaterSurfaceRange = 160.0f;
    static f32 cTurnMotionSpeed = 5.0f;
};  // namespace

bool Mario::isSwimming() const {
    if (isStatusActive(MarioStatus_Swim)) {
        return true;
    }

    return isStatusActive(MarioStatus_Foo);
}

bool Mario::forceStartSwimAndShoot(const Vec3f& rKnockbackVec) {
    WaterInfo waterInfo;
    if (!MR::getWaterAreaObj(&waterInfo, mActor->mPosition)) {
        return false;
    }

    startSwim();
    mSwim->setDamage(rKnockbackVec, 30);
    return true;
}

bool Mario::forceExitSwim() {
    if (!isStatusActive(MarioStatus_Swim)) {
        return false;
    }

    mSwim->mNextAction = MarioSwim::EXIT_ACTION_SURFACE;
    closeStatus(mSwim);
    return true;
}

void MarioSwim::setDamage(const Vec3f& rKnockbackVec, u16 damage) {
    mKnockbackVel = rKnockbackVec;
    mKnockbackTimer = damage;
    mStickInputYInertia = 0.0f;
    mStickInputXInertia = 0.0f;
    mForwardSpeed = 0.0f;
    mSpinDashTimer = 0;
    mSpinTimer = 0;
    mJumpDiveTimer = 0;
    mSinkTimer = 0;
    mZSinkTimer = 0;
    mActionLockTimer = damage;
    mSwimYAccTimer = 0;
    mStateTimer = 0x12C;
    if (mIsOnSurface) {
        mIsOnSurface = false;
    }
}

bool Mario::checkStartSwim() {
    if (isStatusActive(MarioStatus_Swim)) {
        return false;
    }

    if (isStatusActive(MarioStatus_Climb)) {
        return false;
    }

    if (mActionStates.swimExited && getMovementStates().onGround) {
        getAnimator()->waterToGround();
        mActionStates.swimExited = false;
    }

    if (mSwim->checkWaterCube(false)) {
        if (getPlayerMode() == 3) {
            addTrans(-*getGravityVec() * 100.0f);
            if (getPlayer()->getMovementStates().jumping) {
                stopJump();
            }

            if (getPlayer()->isStatusActive(MarioStatus_Wall)) {
                getPlayer()->closeStatus(0);
            }

            if (getPlayer()->isStatusActive(MarioStatus_Blown)) {
                getPlayer()->closeStatus(0);
            }

            return false;
        }

        startSwim();
        return true;
    }

    mSwim->updateUnderWater();
    return false;
}

void Mario::startSwim() {
    if (!isStatusActive(MarioStatus_Swim)) {
        if (getPlayer()->isDamaging()) {
            mSwim->mEntryKind = 4;
            playSound("水落下突入");
        } else if (getPlayer()->getMovementStates().jumping) {
            mSwim->mEntryKind = 1;
            playSound("水落下突入");
        } else {
            mSwim->mEntryKind = 0;
            playSound("水歩行突入");
        }

        changeStatus(mSwim);
        clearSlope();
        stopWalk();
        stopJump();
        mSinkTimer = 0;

        mMovementStates.launchKind = 0;
        if (mMovementStates.frontWall || mMovementStates.sideWall || mMovementStates.backWall) {
            addVelocity(getWallNorm(), 50.0f);
        }

        getPlayer()->lockGroundCheck(mSwim, true);
        checkBaseTransBall();
        mActor->mBindOn = false;
    }
}

MarioSwim::MarioSwim(MarioActor* pActor) : MarioState(pActor, MarioStatus_Swim), mWaterInfo() {
    mHoldOnSurface = 0;
    mIsOnSurface = false;
    mEnteredWater = false;
    mSurfaceStartAnim = false;
    mPaddleJumpReady = false;
    mStrokeQueued = false;
    mUpStrokeAllowed = 0;
    mWallHit = false;
    mIsSwimmingAtSurface = false;
    mIsSinking = false;
    mSlideOut = false;
    mStateTimer = 0;
    mSurfacePaddleHoldTimer = 0;
    mActionLockTimer = 0;
    mSinkTimer = 0;
    mSwimYAccTimer = 0;
    mZSinkTimer = 0;
    mJumpDiveTimer = 0;
    mDashTimer = 0;
    mSurfaceStickTimer = 0;
    mBuoyancyDelayTimer = 0;
    mIdleWaitTimer = 0;
    mWaveFlowTimer = 0;
    mJetCooldownTimer = 0;
    mFloorContactTimer = 0;
    mWaterWallTimer = 0;
    mSurfaceOffset = 0.0f;
    mStickInputYInertia = 0.0f;
    mStickInputXInertia = 0.0f;
    mForwardSpeed = 0.0f;
    mBuoyancySpeed = 0.0f;
    mVerticalAngle = 0.0f;

    mFrontVec.x = 1.0f;
    mFrontVec.y = 0.0f;
    mFrontVec.z = 0.0f;

    mUpVec.x = 0.0f;
    mUpVec.y = 1.0f;
    mUpVec.z = 0.0f;

    mSpinPunchHit = false;
    mSpinTimer = 0;
    mSpinDashTimer = 0;
    mJetKind = 0;
    mJetTimer = 0;
    mRingDashTimer = 0;
    mRingDashChargeTimer = 0;
    mRingDashMaxDuration = 0;
    mRingDashSpeedScale = 0.0f;
    mBlurOffset = 0.0f;
    mPaddleReleased = false;
    mEntryKind = 0;
    mNextAction = EXIT_ACTION_NONE;
    mEffectState = 0xFF;
    mKnockbackVel.zero();
    mDamageType = 0;
    mLargeDamage = 0;
    mKnockbackTimer = 0;
    NebulaMtxIdentity(mUpperBodyJointMtx.toMtxPtr());
    mDrownTimer = 0;
    mCurrentTiltX = 0.0f;
    mCurrentTiltY = 0.0f;
    mOxygenWarningTimer = 0;
    mOxygenDecreaseDelay = 0x78;
    mColdWaterDamageInterval = 0;
    mSwimState = SWIM_STATE_NONE;
    mAreaFollowMtxPos.zero();
    mSurfacePos.set(0.0f, 1.0f, 0.0f);
    mSurfaceNorm.zero();
    mWaterEdgePos.zero();
    mWaterStreamVel.zero();
    mWaterStreamDir.zero();
    mWallStickTimer = 0;
    mWaterDepth = 0.0f;
    mDistToFloor = 0.0f;
    mDistanceToWaterSurface = 0.0f;
    mDeepWater = false;
    mWaterDistanceTarget = 0.0f;
    mSurfaceDepth = 0.0f;
    mMarineSnow = new MarineSnow();
}

void MarioSwim::init() {
    mOxygen = mActor->getConst().getTable()->mOxygenMax;
}

bool MarioSwim::start() {
    mSpinDashTimer = 0;
    mStickInputYInertia = 0.0f;
    mStickInputXInertia = 0.0f;
    mForwardSpeed = 0.0f;
    mSpinTimer = 0;
    mDrownTimer = 0;
    mOxygen = mActor->getConst().getTable()->mOxygenMax;
    mOxygenDecreaseDelay = 0x78;
    mColdWaterDamageInterval = 0;
    mDamageType = 0;
    mWaterWallTimer = 0;
    mOxygenWarningTimer = 180;
    mFloorContactTimer = 0;
    mNextAction = EXIT_ACTION_NONE;
    mRingDashTimer = 0;
    mRingDashChargeTimer = 0;
    mBlurOffset = 0.0f;
    mIdleWaitTimer = 0;
    mEffectState = 0xFF;
    mKnockbackTimer = 0;
    mKnockbackVel.zero();
    mSurfacePaddleHoldTimer = 0;
    mCurrentTiltX = 0.0f;
    mCurrentTiltY = 0.0f;
    mWallStickTimer = 0;
    mStateTimer = 0;
    mSurfaceOffset = 0.0f;
    mWaveFlowTimer = 0;
    mWaterStreamVel.zero();
    mWaterStreamDir.zero();
    mDeepWater = false;
    mWaterDistanceTarget = 0.0f;
    mWallHit = false;
    mSlideOut = false;
    mBuoyancySpeed = 0.0f;

    if (!getPlayer()->getMovementStates().jumping || getPlayer()->mVerticalSpeed <= 100.0f) {
        mBuoyancySpeed = mActor->getConst().getTable()->mSwimUpMaxSpeed;
    }

    mPaddleReleased = true;
    if (checkLvlA()) {
        mPaddleReleased = false;
    }

    mVerticalAngle = DEG_TO_RAD(30);
    onSurface();
    if ((mWaterDepth < -200.0f || mActor->mCameraCut) && mIsOnSurface) {
        mIsOnSurface = false;
    }

    mSurfaceStartAnim = false;
    u32 diveKind = 0, damageEntry = 0;
    mStrokeQueued = false;
    mUpStrokeAllowed = 0;
    if (isAnimationRun("飛び込みジャンプ")) {
        diveKind = 1;
    }

    if (isAnimationRun("後方飛び込みジャンプ")) {
        diveKind = 1;
        getPlayer()->setFrontVecKeepUp(-getFrontVec(), 1.0f);
    }

    if (!mActor->mTakeCount) {
        mJetTimer = 0;
    }

    if (mEntryKind == 4) {
        damageEntry = 1;
    }

    if (mJetTimer != 0 && getPlayer()->getMovementStates().jumping) {
        if (!getPlayer()->mActionStates.jetJumped) {
            diveKind = 2;
        } else {
            getPlayer()->mMovementStates.jumping = false;
        }
    }

    getPlayer()->mActionStates.jetJumped = false;

    if (isAnimationRun("リングダッシュ")) {
        diveKind = 2;
    }

    if (isAnimationRun("水泳スピン移動")) {
        diveKind = 3;
    }

    if (isAnimationRun("水泳スピンジャンプ")) {
        diveKind = 3;
    }

    mFrontVec = getFrontVec();
    MR::normalize(&mFrontVec);
    stopAnimationUpper(nullptr);
    // Basic Swimming
    changeAnimation(nullptr, "水泳基本");
    changeAnimationNonStop("水泳基本");
    getPlayer()->mMovementStates.onGround = false;
    f32 floorDist = mDistToFloor;

    mActionLockTimer = 0;
    mZSinkTimer = 0;
    mSinkTimer = 0;
    mJumpDiveTimer = 0;
    mDashTimer = 0;
    mSurfaceStickTimer = 0;
    mBuoyancyDelayTimer = 0;
    mPaddleJumpReady = false;

    if (floorDist < -mWaterDepth) {
        MarioActor* actor = mActor;
        actor->emitEffectWaterColumn(-mSurfacePos, getTrans() - mSurfacePos * floorDist);
    } else {
        mActor->emitEffectWaterColumn(mSurfacePos, mSurfaceNorm);
    }

    if ((getPlayer()->getMovementStates().jumping && getPlayer()->mVerticalSpeed > 100.0f) || diveKind) {
        Vec3f jumpOffGravity;
        mStateTimer = 0;
        Mario* mario = getPlayer();
        MR::vecKillElement(mario->mJumpVec, getGravityVec(), &jumpOffGravity);
        f32 diveSpeed = jumpOffGravity.length();

        if (getFrontVec().dot(jumpOffGravity) < 0.0f) {
            diveSpeed = 0.0f;
        }

        diveSpeed *= 0.1f;
        diveSpeed = MR::clamp(diveSpeed, 0.0f, 1.0f);

        if (mIsOnSurface) {
            mIsOnSurface = false;
        }

        if (mSwimState == SWIM_STATE_SURFACE) {
            f32 maxSpeed = mActor->getConst().getTable()->mSwimFrontMaxSpeed;
            f32 speedRatio = (0.2f + 0.1f * diveSpeed);
            mJumpDiveTimer = 0x3C;
            mForwardSpeed = speedRatio * maxSpeed;
        } else if (getPlayer()->getMovementStates().hipDrop || diveKind) {
            mForwardSpeed = 10.0f;

            if (mJetTimer != 0) {
                changeAnimationNonStop("水泳ジェット");
            } else {
                changeAnimation("水泳ジャンプダイブ回転");
            }

            playEffect("水面Ｚ沈降");

            switch (diveKind) {
            case 0:
            case 1:
                mActionLockTimer = mActor->getConst().getTable()->mSwimSpecStartTimerA[0];
                mSinkTimer = mActor->getConst().getTable()->mSwimSpecStartTimerB[0];
                break;
            case 2:
                mActionLockTimer = mActor->getConst().getTable()->mSwimSpecStartTimerA[1];
                mSinkTimer = mActor->getConst().getTable()->mSwimSpecStartTimerB[1];
                break;
            case 3:
                mActionLockTimer = mActor->getConst().getTable()->mSwimSpecStartTimerA[2];
                mSinkTimer = mActor->getConst().getTable()->mSwimSpecStartTimerB[2];
                break;
            }

            f32 sinkAngle = ::cLimitAngleSink * (1.0f - diveSpeed);
            mVerticalAngle = diveSpeed * (FloatUtil::PI() * 5.0f / 9.0f) + sinkAngle;
            Vec3f headDown(mFrontVec);
            headDown.y = -10.0f;
            MR::normalize(&headDown);
            getPlayer()->forceSetHeadVecKeepSide(headDown);
        } else if (damageEntry) {
            changeAnimation("水上ダメージ着水");
            mSinkTimer = mActor->getConst().getTable()->mJumpDamageSinkTimer;
            mKnockbackTimer = 0x78;
        } else {
            mForwardSpeed = (0.2f + 0.1f * diveSpeed) * mActor->getConst().getTable()->mSwimFrontMaxSpeed;
            changeAnimation("水泳ジャンプダイブ");

            mActor->setBlendMtxTimer(8);
            mActionLockTimer = 15;
            mSinkTimer = mActor->getConst().getTable()->mJumpSinkTimer;

            mIsSwimmingAtSurface = true;
            mVerticalAngle = 1.49599659443f;
            if (getPlayer()->mVerticalSpeed < 200.0f) {
                f32 speedRatio = (getPlayer()->mVerticalSpeed / 200.0f);
                mActionLockTimer *= speedRatio;
                mSinkTimer *= speedRatio;
                mActor->setBlendMtxTimer((s32)(8.0f * speedRatio));
            }
        }
    } else if (mIsOnSurface) {
        mKnockbackTimer = 0x5A;
        MR::vecKillElement(mActor->getLastMove(), mUpVec, &mKnockbackVel);
        mKnockbackVel.mult(0.5f);
    }

    if (mActor->mTakeCount) {
        if (mJetTimer == 0) {
            if (mActor->getCarrySensor()->isType(ATYPE_JET_TURTLE) || mActor->getCarrySensor()->isType(ATYPE_JET_TURTLE_SLOW)) {
                u32 slowJet = 0;
                if (mActor->getCarrySensor()->isType(ATYPE_JET_TURTLE_SLOW)) {
                    slowJet = 1;
                }

                startJet(slowJet);

                diveKind = 2;
                mIsSwimmingAtSurface = false;
                mVerticalAngle = 2.09439516068f;

                if (mIsOnSurface) {
                    mIsOnSurface = false;
                }
            }
        } else {
            diveKind = 2;
        }
    }

    if (diveKind == 2) {
        if (!isAnimationRun("水泳ジェット")) {
            changeAnimation("水泳ジェット開始", "水泳ジェット");
        } else {
            changeAnimation(nullptr, "水泳ジェット");
        }
    }

    updateLifeByTime();
    if (!getPlayer()->mHitStates.swimLifeDrain) {
        if (!mActor->isExtraPlayer()) {
        MR::getGameSceneLayoutHolder()->changeLifeMeterModeWater();
    }
    }

    mNextAction = EXIT_ACTION_NONE;
    return true;
}

f32 MarioSwim::getSurface() const {
    f32 base = getTrans().y - mWaterDepth;
    f32 amplitude = 40.0f;
    f32 wave;
    f32 swell = 0.01f;
    f32 phaseWobble = 0.03f;
    u16 period = 1800;
    wave = 1.0f + swell * MR::sin((mStateTimer * Angle::cRadian360 / period));
    period = 500;
    f32 phase = phaseWobble * MR::sin((mStateTimer * Angle::cRadian360 / period));
    period = 180;

    return base + wave * (amplitude * (MR::sin(phase + (mStateTimer * Angle::cRadian360 / period)) - 1.0f));
}

bool MarioSwim::update() {
    bool zSinkStarted = false;
    mStateTimer++;
    if (mSlideOut) {
        mNextAction = EXIT_ACTION_SLIDE;
        return false;
    }

    if (!checkWaterCube(false)) {
        if (mNextAction == EXIT_ACTION_JUMP || mNextAction == EXIT_ACTION_SLIDE) {
            return false;
        }

        Vec3f surfaceOffGravity;
        MR::vecKillElement(mSurfaceNorm - getTrans(), getAirGravityVec(), &surfaceOffGravity);
        if (surfaceOffGravity.length() > 100.0f || -mWaterDepth >= getPlayer()->mVerticalSpeed || mKnockbackTimer != 0 || mWallStickTimer != 0) {
            if (!isAnimationRun("水泳ジャンプダイブ")) {
                if (mJetTimer != 0) {
                    doJetJump(1);
                    return false;
                } else {
                    mNextAction = EXIT_ACTION_FALL;
                    return false;
                }
            }
        } else {
            mSwimState = SWIM_STATE_ENTERING;
        }
    }

    if (getPlayer()->mDrawStates.wallGravity && mWaterDepth > -50.0f &&
        MR::diffAngleAbsHorizontal(-getTouchedWallNorm(), getWorldPadDir(), getPlayer()->getAirGravityVec()) < 0.196349546313f) {
        if (calcAngleD(getTouchedWallNorm()) >= 30.0f) {
            mNextAction = EXIT_ACTION_SLIDE;
            return false;
        } else {
            mNextAction = EXIT_ACTION_JUMP;
            return false;
        }
    }

    updateLifeByTime();
    if (mHoldOnSurface) {
        onSurface();
        mSinkTimer = 0;
        mJumpDiveTimer = 0;
        mZSinkTimer = 0;
        mActionLockTimer = 0;
        mStickInputYInertia = 0.0f;
    }

    mUpVec = -getPlayer()->getAirGravityVec();
    f32 fTmp = getSurface() - 30.0f;
    if (mWallStickTimer != 0) {
        mWallStickTimer--;
    }

    if (mIsOnSurface || (mIsSwimmingAtSurface && checkTrgA())) {
        if (mSpinTimer != 0 || mSpinDashTimer != 0) {
            mPaddleReleased = false;
        } else {
            if (mJetTimer != 0) {
                if (checkTrgA()) {
                    doJetJump(0);
                    return false;
                }

                if (getStickY() <= 0) {
                    mVerticalAngle = FloatUtil::PI() / 2.0f;
                    mStickInputYInertia = 1.0f;
                }
            } else {
                u32 paddleHoldFrames = 0x1E;
                // Surface Paddle
                if (isAnimationRun("水上一掻き")) {
                    paddleHoldFrames = 0x1E;
                }

                if (checkLvlA()) {
                    if (mPaddleReleased && (!isAnimationRun("水上一掻き") || !mSurfacePaddleHoldTimer)) {
                        mSurfacePaddleHoldTimer++;
                    }

                    if ((mWallStickTimer != 0 || mJetTimer != 0 || getPlayer()->mMovementStates.frontWall || getPlayer()->mMovementStates.frontWallGlancing) &&
                        checkTrgA()) {
                        mSurfacePaddleHoldTimer = paddleHoldFrames;
                    }
                } else {
                    mPaddleReleased = true;
                    if (mSurfacePaddleHoldTimer && mSurfacePaddleHoldTimer < paddleHoldFrames) {
                        surfacePaddle();
                    }

                    mSurfacePaddleHoldTimer = 0;
                    mPaddleJumpReady = false;
                }

                if (mActor->isRequestJump2P()) {
                    mSurfacePaddleHoldTimer = paddleHoldFrames;
                    getPlayer()->mHitStates.jump2PDelayed = true;
                    getPlayer()->mActionStates.jump2P = true;
                }

                bool canPaddleJump = false;
                if ((!mZSinkTimer || (mSpinTimer != 0 || mSpinDashTimer != 0)) && (mSurfacePaddleHoldTimer >= paddleHoldFrames || mPaddleJumpReady)) {
                    canPaddleJump = true;
                }

                if (mActor->isRequestSpin()) {
                    canPaddleJump = true;
                }

                if (canPaddleJump) {
                    if (mStateTimer > 6) {
                        f32 walkSpeed = MR::clamp(mForwardSpeed / mActor->getConst().getTable()->mSwimToWalkSpd, 0.0f, 1.0f);
                        getPlayer()->mMovementStates.jumpChain = false;
                        getPlayer()->mWalkSpeed = walkSpeed;
                        getPlayer()->tryJump();
                        mNextAction = EXIT_ACTION_POWER_JUMP;
                        return false;
                    }

                    mPaddleJumpReady = true;
                }
            }
        }

        flowOnWave(fTmp);
    } else if (mWaterDepth > -30.0f) {
        if (!mActionLockTimer && !mZSinkTimer && !mSinkTimer && mJetTimer == 0) {
            getAnimator()->forceSetBlendWeight(::cWeightTable);
            changeAnimation("水泳上昇呼吸");
        }

        if (!mSinkTimer) {
            onSurface();
            mActionLockTimer = 0;
        }

        if (mSpinDashTimer > 10) {
            f32 walkSpeed = MR::clamp(mForwardSpeed / mActor->getConst().getTable()->mSwimToWalkSpd, 0.0f, 1.0f);
            getPlayer()->mMovementStates.jumpChain = false;
            getPlayer()->mWalkSpeed = walkSpeed;

            Vec3f lastMove;
            mActor->getLastMove(&lastMove);
            f32 alongGravity = MR::vecKillElement(lastMove, getGravityVec(), &lastMove);
            if (alongGravity > -10.0f) {
                alongGravity = -10.0f;
            }

            lastMove += getGravityVec() * alongGravity;
            getPlayer()->tryForcePowerJump(lastMove, false);
            mNextAction = EXIT_ACTION_POWER_JUMP;
            if (isAnimationRun("水泳スピン移動")) {
                changeAnimationNonStop("水泳スピン移動");
            } else {
                changeAnimationNonStop("水泳スピンジャンプ");
            }

            return false;
        }

        if (mJetTimer != 0 && (getStickY() < -0.1f || checkLvlA()) && getPlayer()->mVerticalSpeed - mWaterDepth > 200.0f) {
            doJetJump(0);
            return false;
        }

        if (mRingDashTimer != 0 && getPlayer()->mVerticalSpeed - mWaterDepth > 200.0f) {
            f32 walkSpeed = MR::clamp(mForwardSpeed / mActor->getConst().getTable()->mSwimToWalkSpd, 0.0f, 1.0f);

            getPlayer()->mMovementStates.jumpChain = false;
            getPlayer()->mWalkSpeed = walkSpeed;
            getPlayer()->tryForcePowerJump(mActor->getLastMove() * 2.0f + mUpVec * 10.0f, false);

            mNextAction = EXIT_ACTION_POWER_JUMP;
            changeAnimationNonStop("リングダッシュ");
            return false;
        }
    } else {
        checkWaterBottom();
        if (getStickY() >= 0.0f && (mWallHit || (mDistToFloor < 60.0f && mForwardSpeed > 1.0f))) {
            const Vec3f& rShadowNorm = getPlayer()->getShadowNorm();
            Vec3f floorSide = mFrontVec.cross(rShadowNorm);
            Vec3f floorFront = rShadowNorm.cross(floorSide);

            if (-mWaterDepth + mDistToFloor > 1000.0f) {
                mEnteredWater = true;
            }

            f32 dot1 = mFrontVec.dot(floorFront);
            f32 dot2 = mFrontVec.dot(rShadowNorm);
            if (dot2 >= 0.0f) {
                mVerticalAngle = DEG_TO_RAD(90) + 0.9f * marioAcos(dot1);
            } else if (mForwardSpeed > 1.0f) {
                mVerticalAngle = DEG_TO_RAD(90) - 0.5f * marioAcos(dot1);
            } else {
                f32 angle = marioAcos(dot1);
                mSurfaceStickTimer = 0x3C;
                mVerticalAngle = DEG_TO_RAD(90) - angle;
            }

            if (mJetTimer != 0 && getPlayer()->mVerticalSpeed < 100.0f) {
                mFloorContactTimer = 2;
                playEffect("水底接触");
                mForwardSpeed *= mActor->getConst().getTable()->mTurboReductionHitFloor;
            }
        }

        mWallHit = false;
    }

    if (mFloorContactTimer != 0) {
        playSound("水底接触");
        if (--mFloorContactTimer == 0) {
            stopEffect("水底接触");
        }
    }

    if (mZSinkTimer != 0 && --mZSinkTimer == 0) {
        zSinkStarted = true;
        mVerticalAngle = mActor->getConst().getTable()->mZsinkAngleX;
        mActionLockTimer = mActor->getConst().getTable()->mZsinkMoveTimer;
        mSinkTimer = mActor->getConst().getTable()->mZsinkSinkTimer;
        mForwardSpeed = mActor->getConst().getTable()->mZsinkFrontSpeed;
        mSurfaceStickTimer = 0x14;
    }

    f32 slowRatio = 1.0f - MR::clamp(mForwardSpeed / mActor->getConst().getTable()->mSwimFrontMaxSpeed, 0.0f, 1.0f);
    if (mDistToFloor - mWaterDepth < 400.0f) {
        slowRatio = 0.0f;
    }

    if (mSurfaceStickTimer != 0) {
        mSurfaceStickTimer--;
    } else {
        f32 pitchRate = slowRatio;
        if (mIsOnSurface && !mZSinkTimer) {
            if (!mHoldOnSurface) {
                if (mJetTimer != 0) {
                    if (getStickY() > 0.707000017166f) {
                        mVerticalAngle = mActor->getConst().getTable()->mZsinkAngleX;
                        mIsSwimmingAtSurface = false;
                        mUpStrokeAllowed = 0;
                    }

                    MarioConstTable* table = mActor->getConst().getTable();
                    f32 a = getStickY();
                    f32 b = table->mSwimRotXIneT;
                    a *= 1.0f - b;
                    mStickInputYInertia *= b;
                    mStickInputYInertia += a;

                    mStickInputXInertia = 2.5f * (getStickX() * (1.0f - mActor->getConst().getTable()->mSwimRotZIneT));
                    f32 stickXBoost = 1.0f;
                    if (getStickY() < 0.0f) {
                        stickXBoost += 2.0f * -getStickY();
                    }

                    mStickInputXInertia *= stickXBoost;
                } else if (checkTrgZ() && !isSpinning()) {
                    stopAnimationUpper("水泳水面初期移動");
                    changeAnimation("水泳潜り");
                    playEffect("水面Ｚ沈降");
                    playSound("水面潜り", -1);
                    mZSinkTimer = mActor->getConst().getTable()->mZsinkStartTimer;
                } else if (!checkLvlZ()) {
                    pitchRate = 0.0f;
                } else {
                    MarioConstTable* table = mActor->getConst().getTable();
                    f32 a = getStickP();
                    f32 b = table->mSwimRotXIne;
                    a *= 1.0f - b;
                    mStickInputYInertia *= b;
                    mStickInputYInertia += a;
                }
            }

            mStickInputXInertia *= mActor->getConst().getTable()->mSwimRotZIne;
        } else {
            if (mRingDashTimer == 0 && mDistToFloor > 200.0f && checkTrgZ() && !mZSinkTimer && !mSinkTimer && mJetTimer == 0 && !isSpinning()) {
                stopAnimation(nullptr);
                changeAnimation("水泳潜り");
                playSound("水中潜り");
                playEffect("水面Ｚ沈降");
                mZSinkTimer = mActor->getConst().getTable()->mZsinkStartTimer;
            }

            if (mJetTimer != 0) {
                MarioConstTable* table = mActor->getConst().getTable();
                f32 a = getStickY();
                f32 b = table->mSwimRotXIneT;
                // b = getSwimValue(a = getStickY(), 2, mActor->getConst().getTable());
                a *= 1.0f - b;
                mStickInputYInertia *= b;
                mStickInputYInertia += a;
                table = mActor->getConst().getTable();
                a = getStickX();
                b = table->mSwimRotZIneT;
                a *= 1.0f - b;
                mStickInputXInertia *= b;
                mStickInputXInertia += a;
            } else {
                MarioConstTable* table = mActor->getConst().getTable();
                f32 a = getStickY();
                f32 b = table->mSwimRotXIne;

                // b = getSwimValue(a = getStickY(), 0, mActor->getConst().getTable());
                a *= 1.0f - b;
                mStickInputYInertia *= b;
                mStickInputYInertia += a;
                table = mActor->getConst().getTable();
                a = getStickX();

                // b = getSwimValue(a = getStickX(), 1, mActor->getConst().getTable());
                b = table->mSwimRotZIne;

                a *= 1.0f - b;

                mStickInputXInertia *= b;
                mStickInputXInertia += a;
            }
        }

        if (!mIsSwimmingAtSurface && !mIdleWaitTimer && mActionLockTimer != 0) {
            mVerticalAngle += pitchRate * (mStickInputYInertia * mActor->getConst().getTable()->mSwimRotSpeedX);
        }

        if ((mForwardSpeed < ::cTurnMotionSpeed || mIsOnSurface || checkLvlA() || checkLvlZ()) && isAnimationRun("水泳一掻き") &&
            isAnimationTerminate("水泳一掻き")) {
            stopAnimation(nullptr);
        }

        if (!checkLvlA() && !checkLvlZ() && !mActionLockTimer && mForwardSpeed < ::cTurnMotionSpeed) {
            bool worthless;
            bool& worthlesser = worthless;
            funReferenceTime(worthlesser);
            if (!mIsSwimmingAtSurface && mKnockbackTimer == 0 && mWaterDepth < -400.0f) {
                mIdleWaitTimer++;
                if (MR::getRandom() < 0.03f) {
                    playSound("水中ウエイト");
                }
            }

            if (mIdleWaitTimer >= 0x78) {
                mIdleWaitTimer = 0x78;
            }
        } else {
            mIdleWaitTimer = 0;
        }

        f32 targetAngle = 0.0f;
        if (MR::isNearZero(getStickY(), 0.1f)) {
            f32 num = mIdleWaitTimer / 120.0f;
            if (num > 1.0f) {
                num = 1.0f;
            }

            targetAngle = mVerticalAngle + num * (::cNeutralAngleWait - mVerticalAngle);
        } else if (getStickY() > 0.0f) {
            f32 minBlend = 0.0f;
            f32 num = mIdleWaitTimer / 120.0f;
            if (mForwardSpeed < 2.0f) {
                minBlend = 1.0f;
            } else {
                bool confusion;
                bool& confusionion = confusion;
                if (mForwardSpeed > 10.0f) {
                    confusionion = true;
                } else {
                    minBlend = 1.0f - (mForwardSpeed - 2.0f) / 8;
                }
            }

            if (num < minBlend) {
                num = minBlend;
            }

            f32 numClamped = MR::clamp(num, 0.0f, 1.0f);
            f32 limitAngle = numClamped * ::cLimitAngleWait + (1.0f - numClamped) * ::cLimitAngleSink;
            if (mDistToFloor < 300.0f && -mWaterDepth < 100.0f) {
                limitAngle = ::cNeutralAngleWait;
            }

            targetAngle = ::cNeutralAngleWait + (limitAngle - ::cNeutralAngleWait) * getStickY();
        } else if (getStickY() < 0.0f) {
            targetAngle = ::cNeutralAngleWait + (::cUpperAngleWait - ::cNeutralAngleWait) * -getStickY();
        }

        f32 angleBlend = 0.05f;
        if (mForwardSpeed > 5.0f && (angleBlend -= angleBlend * (5.0f / mForwardSpeed)) < 0.0f) {
            angleBlend = 0.0f;
        }

        if (mJetTimer != 0) {
            switch (mJetKind) {
            case 0:
                angleBlend *= mActor->getConst().getTable()->mSwimXJetRotRatio;
                break;
            default:
                angleBlend *= mActor->getConst().getTable()->mSwimXJetRotRatioRed;
                break;
            }
        }

        if (mIsSwimmingAtSurface) {
            targetAngle = 0.523598790169f;
            angleBlend = 0.01f;
        }

        if (mIsOnSurface) {
            targetAngle = 0.523598790169f;
        }

        if (mZSinkTimer != 0) {
            angleBlend = 0.15f;
            targetAngle = mActor->getConst().getTable()->mZsinkAngleX;
        }

        bool endTurnDown = true;
        mVerticalAngle = mVerticalAngle * (1.0f - angleBlend) + targetAngle * angleBlend;
        if (mIdleWaitTimer != 0 && mKnockbackTimer == 0 && !mZSinkTimer && getStickY() > 0.0f) {
            f32 tiltLimit = PI / mActor->getConst().getTable()->mSwimTiltReal;
            f32 stickY = getStickY();
            targetAngle = 0.523598790169f + stickY * (tiltLimit - 0.523598790169f);
            if (mVerticalAngle < targetAngle) {
                mVerticalAngle =
                    mVerticalAngle * mActor->getConst().getTable()->mSwimTiltSpd + targetAngle * (1.0f - mActor->getConst().getTable()->mSwimTiltSpd);
            } else {
                mVerticalAngle = mVerticalAngle * (1.0f - angleBlend) + targetAngle * angleBlend;
            }

            if (!isSpinning() && mJetTimer == 0) {
                changeAnimation("水泳ターン下");
            }

            endTurnDown = false;
        }

        if (endTurnDown) {
            stopAnimation("水泳ターン下");
        }

        if (mZSinkTimer != 0) {
            mVerticalAngle = MR::clamp(mVerticalAngle, 0.523598790169f, mActor->getConst().getTable()->mZsinkAngleX);
        } else {
            mVerticalAngle = MR::clamp(mVerticalAngle, ::cUpperAngleWait, ::cLimitAngleSink);
        }

        if (MR::isStageSwimAngleLimit()) {
            mVerticalAngle = MR::clamp(mVerticalAngle, 0.872664690018f, 2.26892805099f);
        }
    }

    if ((mIsSwimmingAtSurface || (mIsOnSurface && isStickOn())) && mJetTimer == 0) {
        Vec3f padDir(getWorldPadDir());
        f32 turnRate;
        if (mIsOnSurface) {
            turnRate = 0.01f * (12.0f - mForwardSpeed);
            if (turnRate <= 0.01f) {
                turnRate = 0.01f;
            }
        } else {
            turnRate = 0.01f * (10.0f - mForwardSpeed);
        }

        if (mJetTimer != 0) {
            turnRate *= 3.0f;
        }

        f32 maxTurn = MR::clamp(turnRate * mActor->getConst().getTable()->mSwimRotSpeedRatioSurface, 0.0f, 1.0f);
        f32 diffAngle = MR::diffAngleAbs(mFrontVec, padDir);
        if (diffAngle > 0.0f) {
            if (diffAngle < maxTurn) {
                mFrontVec = padDir;
            } else {
                MR::vecBlendSphere(mFrontVec, padDir, &mFrontVec, maxTurn / diffAngle);
            }
        }
    } else {
        f32 rotSpeed = slowRatio + mActor->getConst().getTable()->mSwimRotSpeedZStop;
        MR::rotAxisVecRad(mFrontVec, -mUpVec, &mFrontVec, rotSpeed * (mStickInputXInertia * mActor->mConst->getTable()->mSwimRotSpeedZ));
    }

    MR::vecKillElement(mFrontVec, mUpVec, &mFrontVec);
    Vec3f targetFront;
    Vec3f side;
    if (!MR::normalizeOrZero(&mFrontVec)) {
        side.cross(mUpVec, mFrontVec);
        MR::normalize(&side);
        getPlayer()->setSideVec(side);
        MR::rotAxisVecRad(mFrontVec, side, &targetFront, mVerticalAngle);
        Vec3f front(getFrontVec());
        f32 frontBlend = 0.1f;
        if (zSinkStarted) {
            frontBlend = 1.0f;
        }

        Vec3f blendedFront;
        MR::vecBlendSphere(front, targetFront, &blendedFront, frontBlend);
        getPlayer()->setFrontVecKeepSide(blendedFront);
    } else {
        targetFront = getFrontVec();
        side = getPlayer()->mSideVec;
    }

    spin();
    Vec3f moveAxis;
    moveAxis = getPlayer()->mPostureUpVec;
    if (mJumpDiveTimer != 0 || mSinkTimer != 0 || isAnimationRun("水泳ジャンプダイブ回転") || isAnimationRun("水泳潜り")) {
        moveAxis.cross(targetFront, side);
    }

    if (mJetTimer == 0) {
        decideVelocity();
    }

    Vec3f swimVel = moveAxis * mForwardSpeed;
    if (mIsOnSurface && mUpVec.dot(moveAxis) > 0.0f) {
        f32 mag = swimVel.length();
        MR::vecKillElement(swimVel, mUpVec, &swimVel);
        swimVel.setLength(mag);
    }

    f32 element = MR::vecKillElement(swimVel, mUpVec, &swimVel);
    f32 yAccRamp;
    if (mSwimYAccTimer > 0x19) {
        yAccRamp = (50 - mSwimYAccTimer) / 25.0f;
    } else {
        yAccRamp = mSwimYAccTimer / 25.0f;
    }

    f32 yRatio = yAccRamp + (1.0f - yAccRamp) * mActor->getConst().getTable()->mSwimSpdYratio;

    swimVel += mUpVec * element * yRatio;
    addVelocity(swimVel);
    u16 sinkFrames = mSinkTimer;
    if (sinkFrames) {
        addVelocity(getGravityVec(), 0.25f * sinkFrames);
        mSinkTimer--;
    }

    if (mJumpDiveTimer != 0) {
        mJumpDiveTimer--;
    }

    if (!mIsOnSurface && !mActionLockTimer && mJetTimer == 0) {
        procBuoyancy();
    } else if (mUpVec.dot(moveAxis) < 0.0f) {
        if (mIsOnSurface) {
            mIsOnSurface = false;
        }
    } else if (mIsOnSurface) {
        AreaObj* obj = MR::getAreaObj("WaterArea", getTrans() + getGravityVec() * 100.0f);
        if (obj != nullptr) {
            Pos3f* followMtx = obj->getFollowMtx();
            if (followMtx != nullptr) {
                Vec3f areaPos;
                MR::extractMtxTrans(followMtx->toMtxPtr(), &areaPos);

                if (areaPos != mAreaFollowMtxPos) {
                    if ((areaPos - mAreaFollowMtxPos).length() < 10.0f) {
                        addVelocity(areaPos - mAreaFollowMtxPos);
                    }

                    mAreaFollowMtxPos = areaPos;
                }
            }
        }
    }

    if (mKnockbackTimer != 0) {
        mKnockbackTimer--;
        addVelocity(mKnockbackVel);
        if (mKnockbackTimer < 0x78) {
            mKnockbackVel.mult(0.98f);
        }
    }

    if (mJetTimer != 0) {
        addVelocity(mWaterStreamVel, mActor->getConst().getTable()->mTurboReductionStream);
    } else {
        addVelocity(mWaterStreamVel);
    }

    Vec3f streamDir(mWaterStreamVel);
    f32 streamSpeed = streamDir.length();
    if (!MR::normalizeOrZero(&streamDir)) {
        if (MR::diffAngleAbs(streamDir, mFrontVec) < PI / 2) {
            MR::vecBlendSphere(mFrontVec, streamDir, &mFrontVec, 0.001f * streamSpeed);
        } else {
            MR::vecBlendSphere(mFrontVec, -streamDir, &mFrontVec, 0.001f * streamSpeed);
        }
    }

    Vec3f whirlAccel;
    if (MR::calcWhirlPoolAccelInfo(getTrans(), &whirlAccel) && !MR::isNearZero(whirlAccel)) {
        mWaterStreamVel += whirlAccel;
        Vec3f whirlDir(whirlAccel);
        MR::normalizeOrZero(&whirlDir);
        MR::vecBlendSphere(mFrontVec, whirlDir, &mFrontVec, 0.1f);
    }

    if (mJetTimer == 0 && !mIsOnSurface && !MR::isNearZero(mWaterStreamDir)) {
        f32 angleDiff = MR::diffAngleSignedHorizontal(mWaterStreamDir, mFrontVec, getPlayer()->getAirGravityVec());
        f32 cmp = DEG_TO_RAD(60);
        if (angleDiff > cmp) {
            Mtx rotLeft;
            NebulaMtxRotAxisRad(rotLeft, &mUpVec, angleDiff - DEG_TO_RAD(60));
            NebulaMtxMultVecSR(rotLeft, &mFrontVec, &mFrontVec);
        } else if (angleDiff < -cmp) {
            Mtx rotRight;
            NebulaMtxRotAxisRad(rotRight, &mUpVec, -(-angleDiff - DEG_TO_RAD(60)));
            NebulaMtxMultVecSR(rotRight, &mFrontVec, &mFrontVec);
        }
    }

    decideAnimation();
    decideEffect(false);

    if (mActionLockTimer != 0) {
        mActionLockTimer--;
    }

    if (mSwimYAccTimer != 0) {
        mSwimYAccTimer--;
    }

    if (mWaterWallTimer != 0) {
        mWaterWallTimer--;
    }

    updateTilt();
    jet();
    pushedByWaterWall();
    return true;
}

void MarioSwim::procBuoyancy() {
    if (mNextAction != EXIT_ACTION_NONE) {
        return;
    }

    if (mKnockbackTimer != 0) {
        mBuoyancySpeed = 0.0f;
    } else {
        mIsSinking = false;
        if (mEnteredWater) {
            mEnteredWater = false;
            mBuoyancyDelayTimer = 120;
        }

        f32 waterDepthNeg = -mWaterDepth;
        f32 buoyancyScale = 1.0f;
        f32 accelerationScale = 1.0f;
        const MarioConstTable* table = mActor->getConst().getTable();
        bool isBelowStrongSwimDepth = table->mSwimUpStrongDist > waterDepthNeg;

        f32 swimUpWeakDist = table->mSwimUpWeakDist;
        f32 swimUpBottomDist = table->mSwimUpBottomDist;
        f32 floorDepthSum = waterDepthNeg + mDistToFloor;

        if (floorDepthSum < 2000.0f) {
            f32 ratio = floorDepthSum / 2000.0f;
            swimUpWeakDist = table->mSwimUpWeakDist * ratio;
            swimUpBottomDist = table->mSwimUpBottomDist * ratio;
        }

        bool shouldSink = (swimUpWeakDist > mDistToFloor);
        if (mForwardSpeed > 2.0f && mVerticalAngle > PI / 2) {
            shouldSink = false;
            mIsSinking = false;
        }

        if (!(mDistToFloor < 300.0f) || !(-mWaterDepth < 100.0f)) {
            if (shouldSink) {
                if (mStateTimer > 300) {
                    f32 distRatio = mDistToFloor / swimUpWeakDist;
                    if (distRatio < 0.0f) {
                        distRatio = 0.0f;
                    }

                    if (swimUpBottomDist > mDistToFloor) {
                        distRatio = 0.0f;
                    }

                    buoyancyScale =
                        mActor->getConst().getTable()->mSwimUpWeakRatio + (distRatio * (1.0f - mActor->getConst().getTable()->mSwimUpWeakRatio));
                } else {
                    buoyancyScale = 1.0f + (1.0f - (static_cast< f32 >(mStateTimer) / 300.0f));
                }

                mIsSinking = true;
            } else if (isBelowStrongSwimDepth) {
                const MarioConst& marioConst = mActor->getConst();
                MarioConstTable* table = mActor->getConst().getTable();

                if (table->mSwimUpSurfaceDist > waterDepthNeg) {
                    buoyancyScale = table->mSwimUpStrongRatio;
                } else {
                    f32 strongRatio = (waterDepthNeg - table->mSwimUpSurfaceDist) / (table->mSwimUpStrongDist - table->mSwimUpSurfaceDist);
                    if (strongRatio < 0.0f) {
                        strongRatio = 0.0f;
                    }

                    buoyancyScale = strongRatio + ((1.0f - strongRatio) * marioConst.getTable()->mSwimUpStrongRatio);
                }
            }
        }

        if (mBuoyancyDelayTimer != 0) {
            mBuoyancyDelayTimer--;
        } else {
            f32 ratio = 1.0f;
            mBuoyancySpeed += (accelerationScale * (ratio * mActor->getConst().getTable()->mSwimUpAcc));
        }

        if (mBuoyancySpeed > mActor->getConst().getTable()->mSwimUpMaxSpeed) {
            mBuoyancySpeed = mActor->getConst().getTable()->mSwimUpMaxSpeed;
        }

        if (!MR::isNearZero(mWaterStreamVel)) {
            buoyancyScale = 1.0f;
        }

        if (mIsSinking) {
            if (mDistToFloor < mActor->getConst().getTable()->mSwimUpBottomDist) {
                f32 clampRatio = MR::clamp(1.0f - (static_cast< f32 >(mActionLockTimer) / 50.0f), 0.0f, 1.0f);
                f32 sinkFactor = ((1.0f - clampRatio) + (clampRatio * mActor->getConst().getTable()->mSwimReverseSinkRatio));
                sinkFactor *= 0.1f;

                if (!MR::isNearZero(mWaterStreamVel)) {
                    sinkFactor = 1.0f;
                }

                addVelocity(-mUpVec, (sinkFactor * (mBuoyancySpeed * buoyancyScale)));
            } else {
                addVelocity(-mUpVec, mBuoyancySpeed * buoyancyScale);
            }
        } else {
            addVelocity(mUpVec, mBuoyancySpeed * buoyancyScale);
        }
    }
}

void MarioSwim::flowOnWave(f32 unused) {
    if (mActionLockTimer != 0) {
        return;
    }

    if (mWaterDepth < -::cWaterSurfaceRange) {
        if (mIsOnSurface) {
            mIsOnSurface = false;
        }

        return;
    } else if (mIsOnSurface) {
        if (mWaterDepth < -25.0f) {
            addVelocity(mSurfacePos, mBuoyancySpeed);
        }

        if (mWaterDepth > 0.0f) {
            Vec3f inverseFlow = -mSurfacePos;
            addVelocity(inverseFlow, 5.0f + mWaterDepth);
        }
    }

    mWaveFlowTimer++;
}

void MarioSwim::decideVelocity() {
    if (mRingDashTimer != 0) {
        return;
    }

    if (MarioModule::checkLvlZ() || (MarioModule::checkLvlA() && mPaddleReleased)) {
        mIsSwimmingAtSurface = false;
    }

    if (mWaterDepth < -400.0f && mSinkTimer == 0) {
        mIsSwimmingAtSurface = false;
    }

    if ((mIsOnSurface && (MarioModule::isStickOn() || mActionLockTimer != 0)) || (mIsSwimmingAtSurface && MarioModule::isStickOn())) {
        if (mActionLockTimer == 0 && mJetTimer == 0) {
            f32 angleDiff = MR::diffAngleAbs(mFrontVec, getWorldPadDir());
            f32 angleFactor = (PI - angleDiff) / PI;
            angleFactor *= MarioModule::getStickP();
            const MarioConst& marioConst = mActor->getConst();
            MarioConstTable* table = mActor->getConst().getTable();
            f32 inertia = table->mSwimFrontIne;
            if (mSpinDashTimer == 0 && mForwardSpeed >= (angleFactor * table->mSwimFrontOnWaterSpeed)) {
                inertia = 0.95f;
            }

            mForwardSpeed = mForwardSpeed * inertia + (1.0f - inertia) * (angleFactor * marioConst.getTable()->mSwimFrontOnWaterSpeed);
        }
    } else if (mIsOnSurface && mDashTimer != 0) {
        f32 inertia = mActor->getConst().getTable()->mSwimFrontIne;
        mForwardSpeed = (mForwardSpeed * inertia) + ((1.0f - inertia) * mActor->getConst().getTable()->mSwimFrontOnWaterSpeed);
    } else {
        if (!mIsOnSurface && checkTrgA() && mKnockbackTimer == 0 && !isSpinning()) {
            f32 stickX = MR::abs(MarioModule::getStickX());
            f32 stickFactor = 1.0f - (0.3f * stickX);
            f32 threshold = 0.75f * stickFactor;

            if (mVerticalAngle < PI / 2 && getStickY() > threshold && mActionLockTimer == 0 && mUpStrokeAllowed != 0) {
                f32 stickY = getStickY();
                f32 normalizedY = (stickY - threshold) / (1.0f - threshold);
                mVerticalAngle =
                    ((1.0f - normalizedY) * (FloatUtil::PI() * 2.0f / 3.0f)) + (normalizedY * (FloatUtil::PI() / 1.2f));
            } else if (mVerticalAngle < PI / 2 && MR::abs(getStickY()) < 0.1f && mActionLockTimer == 0 && mUpStrokeAllowed != 0) {
                mVerticalAngle = PI / 2;
            }

            mUpStrokeAllowed = 0;

            if (!MarioModule::isAnimationTerminate("水泳一掻き")) {
                if (mStrokeQueued) {
                    return;
                }

                const MarioConst& marioConst = mActor->getConst();
                f32 frame = getAnimator()->getFrame();
                MarioConstTable* table = marioConst.getTable();
                if (!MR::isInRange(frame, static_cast< f32 >(table->mSwimConnectIn), static_cast< f32 >(table->mSwimConnectOut))) {
                    mStrokeQueued = true;
                    return;
                }
            }

            mStrokeQueued = false;
            mActionLockTimer = 60;
            mSwimYAccTimer = 50;
            if (!MarioModule::isAnimationRun("水泳潜り") && !MarioModule::isAnimationRun("水泳ジャンプダイブ回転")) {
                getAnimator()->forceSetBlendWeight(::cWeightTableSP);
                stopAnimation(nullptr);
                changeAnimationNonStop("水泳一掻き");
                playSound("水中一掻き");
                mDashTimer = 40;
            }
        } else {
            if (!mIsOnSurface && MarioModule::checkLvlA()) {
                mForwardSpeed = (mForwardSpeed * mActor->getConst().getTable()->mSwimFrontIne) +
                                (mActor->getConst().getTable()->mSwimFrontNormalSpeed * (1.0f - mActor->getConst().getTable()->mSwimFrontIne));
                mBuoyancySpeed = 0.0f;
            } else {
                if (mIsOnSurface) {
                    mForwardSpeed *= mActor->getConst().getTable()->mSwimStopIneSurface;
                } else {
                    mForwardSpeed *= mActor->getConst().getTable()->mSwimStopIne;
                }

                if (MarioModule::isAnimationRun("水泳上昇呼吸")) {
                    mForwardSpeed *= 0.95f;
                }

                if (mActor->getConst().getTable()->mSwimUpBottomDist > mDistToFloor) {
                    mForwardSpeed *= 0.99f;
                } else if (mActor->getConst().getTable()->mSwimUpWeakDist > mDistToFloor) {
                    mForwardSpeed *= 0.995f;
                }
            }
        }
    }

    if (mDashTimer != 0) {
        bool spinning = (mSpinTimer != 0 || mSpinDashTimer != 0);

        if (spinning) {
            if (mIsOnSurface) {
                if (mDashTimer < 20) {
                    f32 acc = ::cFrontAccSpinSurface[20 - mDashTimer];

                    if (acc < 0.0f) {
                        mForwardSpeed += acc * mActor->getConst().getTable()->mSwimSpinSurfaceAccRatioR;
                    } else {
                        mForwardSpeed += acc * mActor->getConst().getTable()->mSwimSpinSurfaceAccRatio;
                    }

                    playSound("水面スピン");
                } else {
                    mForwardSpeed *= 0.96f;
                }
            } else {
                if (mDashTimer < 30) {
                    f32 acc = ::cFrontAccSpin[30 - mDashTimer];
                    if (acc < 0.0f) {
                        mForwardSpeed += acc * mActor->getConst().getTable()->mSwimSpinAccRatioR;
                    } else {
                        mForwardSpeed += acc * mActor->getConst().getTable()->mSwimSpinAccRatio;
                    }

                    playSound("水中スピン");
                } else {
                    mForwardSpeed *= 0.96f;
                }
            }
        } else if (mDashTimer < 40) {
            f32 minRatio;
            f32 speedRatio;
            f32 speed;
            f32 acc;
            acc = ::cFrontAcc[40 - mDashTimer];
            const MarioConstTable* table = mActor->getConst().getTable();
            speed = mForwardSpeed;
            speedRatio = speed / table->mSwimFrontMaxSpeed;
            minRatio = table->mSwimAccMinRatio;
            mForwardSpeed += (minRatio + speedRatio * (1.0f - minRatio)) * (acc * table->mSwimAccRatio);
        }

        mDashTimer--;
        bool canClamp = isSpinning();

        if (canClamp) {
            if (mForwardSpeed > mActor->getConst().getTable()->mSwimFrontSpinSpeed) {
                mForwardSpeed = mActor->getConst().getTable()->mSwimFrontSpinSpeed;
            }
        } else {
            if (mForwardSpeed > mActor->getConst().getTable()->mSwimFrontMaxSpeed) {
                mForwardSpeed = mActor->getConst().getTable()->mSwimFrontMaxSpeed;
            }
        }
    }

    if (mZSinkTimer != 0) {
        mForwardSpeed *= 0.95f;
    }
}

void MarioSwim::decideAnimation() {
    f32 animSpeeds[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    animSpeeds[1] = mActor->mConst->getTable()->mSwimSurfaceSpeed;
    animSpeeds[2] = mActor->getConst().getTable()->mSwimWaterSpeed;
    animSpeeds[3] = mActor->getConst().getTable()->mSwimDriftSpeed;

    if (mJetTimer != 0) {
        return;
    }

    if (isAnimationRun("水泳水面初期移動")) {
        if (getStickP() < 0.1f) {
            stopAnimationUpper(nullptr);
        }
    }

    if (isAnimationRun(nullptr) && !isAnimationRun("水泳上昇呼吸")) {
        if (MR::abs(getStickX()) < 0.1f) {
            stopAnimation("水泳ターン左");
            stopAnimation("水泳ターン右");
        }

        return;
    }

    if (mActionLockTimer != 0) {
        getAnimator()->setBlendWeight(&::cWeightTable[4], mActor->getConst().getTable()->mWeightBlendRatioSwim);
        return;
    }

    u32 animIndex = 0;

    if (!mIsOnSurface) {
        if (checkLvlA()) {
            playSound("水中バタ足");
            animIndex = 2;
        } else {
            animIndex = 3;
            if (mIsSwimmingAtSurface != false) {
                if (getStickP() > 0.1f) {
                    animIndex = 1;
                }
            } else if (MR::abs(getStickX()) > 0.1f && mVerticalAngle < 1.6534699f && mForwardSpeed < ::cTurnMotionSpeed) {
                if (getStickX() < 0.0f) {
                    changeAnimation("水泳ターン左");
                } else {
                    changeAnimation("水泳ターン右");
                }

                return;
            }
        }
    } else {
        // Surface logic
        if (getStickP() > 0.1f) {
            playSound("水面バタ足");

            if (isAnimationRun("水泳上昇呼吸")) {
                stopAnimation("水泳上昇呼吸");
                if (mIsSwimmingAtSurface == false) {
                    mSurfaceStartAnim = true;
                }
            }

            animIndex = 1;

            if (mSurfaceStartAnim) {
                getAnimator()->forceSetBlendWeight(&::cWeightTable[4]);
                changeAnimationUpper("水泳水面初期移動");
                mSurfaceStartAnim = false;
                return;
            }
        } else {
            if (mDashTimer == 0 && mForwardSpeed < 2.0f) {
                mSurfaceStartAnim = true;
            }

            animIndex = 0;
            if (isAnimationRun(nullptr)) {
                return;
            }
        }
    }

    if (animIndex == 1) {
        getAnimator()->setBlendWeight(&::cWeightTable[animIndex * 4], mActor->getConst().getTable()->mWeightBlendRatioSwimB);
    } else {
        getAnimator()->setBlendWeight(&::cWeightTable[animIndex * 4], mActor->getConst().getTable()->mWeightBlendRatioSwim);
    }

    getAnimator()->setSpeed(animSpeeds[animIndex]);
}

void MarioSwim::decideEffect(bool isReset) {
    u8 oldState = mEffectState;
    if (mIsOnSurface) {
        if (isAnimationRun("水泳水面初期移動") || mForwardSpeed > 2.5f) {
            mEffectState = 1;
        } else {
            mEffectState = 0;
        }

    } else {
        if (checkLvlZ() || checkLvlA() || mActionLockTimer != 0 || isAnimationRun(nullptr)) {
            mEffectState = 2;
        } else {
            mEffectState = 3;
        }
    }

    if (isReset == true) {
        mEffectState = 0xFF;
    }

    if (oldState != mEffectState) {
        switch (oldState) {
        case 0:
            if (mEffectState != 1) {
                stopEffect("水面ウエイト波紋");
            }

            break;

        case 1:
            stopEffect("水面泳ぎ");
            if (mEffectState != 0) {
                stopEffect("水面ウエイト波紋");
            }

            break;

        case 2:
            stopEffect("水中身体泡");
            break;
        }

        if ((mEffectState < 2 && oldState >= 2) || isReset) {
            stopEffect("水中口泡");
        }

        switch (mEffectState) {
        case 0: {

            break;
        }

        case 1:
            if (mSurfaceOffset > -10.0f) {
                Vec3f projected;
                MR::vecKillElement(mActor->mHeadPos - mSurfaceNorm, mSurfacePos, &projected);

                const Vec3f& lastMove = mActor->getLastMove();

                playEffectRTW("水面泳ぎ", mSurfacePos, mSurfaceNorm + projected + lastMove);
            } else {
                stopEffect("水面泳ぎ");
            }

            break;
        case 2:
            playEffect("水中身体泡");

        case 3:
            playEffect("水中口泡");
            break;
        }
    }

    if (mEffectState <= 1) {
        Vec3f projected;
        MR::vecKillElement(mActor->mHeadPos - mSurfaceNorm, mSurfacePos, &projected);

        const Vec3f& lastMove = mActor->getLastMove();

        playEffectRTW("水面ウエイト波紋", mSurfacePos, mSurfaceNorm + projected + lastMove);

        if (MR::getRandom() < 0.03f) {
            playSound("水面ウエイト");
        }
    }
}

bool MarioSwim::notice() {
    if (MarioState::getNoticedStatus() == MarioStatus_FpView) {
        return true;
    }

    return getNoticedStatus() == MarioStatus_Paralyze;
}

bool MarioSwim::close() {
    if (mIsOnSurface) {
        mIsOnSurface = false;
    }

    stopAnimationUpper(nullptr);
    stopAnimation("水上スピン");        // Surface Spin
    stopAnimation("水泳スピン");        // Swim Spin
    stopAnimation("水泳ジェット開始");  // Swim Jet Start
    stopEffect("水底接触");             // Water Bottom Contact
    stopEffect("水泳スピン");           // Swim Spin Effect

    if (mActor->getCarrySensor()) {
        LiveActor* shell = mActor->getCarrySensor()->mHost;
        MR::forceDeleteEffect(shell, "BrakeLamp");
    }

    setYangleOffset(0.0f);
    u32 jointID = getAnimator()->getUpperJointID();
    setJointGlobalMtx(jointID, nullptr);

    decideEffect(true);

    if (mNextAction != EXIT_ACTION_SURFACE) {
        // If not surfacing, decide between Fall or Basic based on vertical state
        if (getPlayer()->mMovementStates.jumping) {
            stopAnimation("水泳基本", "落下");
        } else {
            stopAnimation("水泳基本", "基本");
        }
    } else {
        if (isDefaultAnimationRun("水泳基本")) {
            changeAnimation(nullptr, "基本");
        }
    }

    mWaterInfo.clear();

    if (mNextAction == EXIT_ACTION_JUMP || mNextAction == EXIT_ACTION_SLIDE) {
        mActor->setBlendMtxTimer(16);
        changeAnimation("水泳陸うちあげ");  // Landing from water
        playSound("水歩行脱出");

        f32 stickP = getStickP();
        getPlayer()->mWalkSpeed = stickP;

        getPlayer()->mMovementStates.groundMissing = false;
        getPlayer()->mMovementStates.onGround = true;
        getPlayer()->updateGroundInfo();
        getPlayer()->mDrawStates.snappedToGround = true;
        getPlayer()->mMovementStates.groundMissing = false;
        getPlayer()->mMovementStates.onGround = true;

        Vec3f groundPos;
        mActor->getGroundPos(&groundPos);

        if ((groundPos - getTrans()).length() >= 100.0f || isSlipPolygon(getGroundPolygon())) {
            mNextAction = EXIT_ACTION_POWER_JUMP;

            getPlayer()->tryForcePowerJump(mFrontVec * 5.0f - getAirGravityVec() * 15.0f, false);
            Mario* player = getPlayer();
            player->mSlipInhibitTimer = 120;
        } else {
            // Snap to ground
            addTrans(groundPos - getTrans());
            Mario* player = getPlayer();
            player->mSlipInhibitTimer = 60;
        }
    } else {
        getPlayer()->forceSetHeadVecKeepSide(-getGravityVec());
        changeAnimationInterpoleFrame(6);
        mActor->setBlendMtxTimer(4);
    }

    if (mNextAction == EXIT_ACTION_POWER_JUMP) {

        playEffectRT("水面ジャンプ水柱", mSurfacePos, mSurfaceNorm);
        playSound("水ジャンプ脱出");

        mDeepWater = true;
        mWaterDistanceTarget = 0.0f;
        mSurfaceDepth = 0.0f;

        getPlayer()->setGroundNorm(-getAirGravityVec());
    }

    if (mNextAction == EXIT_ACTION_FALL) {
        Vec3f trans = getTrans();

        playEffectRT("水面ジャンプ水柱", -mSurfacePos, trans + mSurfacePos * mDistToFloor);

        getPlayer()->setGroundNorm(-getAirGravityVec());

        if (mJetTimer != 0) {
            changeAnimationNonStop("水泳ジェット");
        }
    }

    if (mNextAction == EXIT_ACTION_SLIDE) {
        getPlayer()->connectToClimb();
    }

    if (mNextAction == EXIT_ACTION_NONE) {
        getPlayer()->tryDrop();

        MR::vecKillElement(mWaterStreamVel, getGravityVec(), &mWaterStreamVel);

        getPlayer()->mJumpVec = getPlayer()->getJumpVec() + mWaterStreamVel * 0.3f;
    }

    getPlayer()->unlockGroundCheck(this);
    mActor->mBindOn = true;
    mSwimState = SWIM_STATE_NONE;

    mActor->resetWaterLife();
    if (!mActor->isExtraPlayer()) {
        MR::getGameSceneLayoutHolder()->changeLifeMeterModeGround();
    }

    mBlurOffset = 0.0f;
    if (mJetTimer != 0) {
        getPlayer()->mActionStates.swimExited = true;
    }

    getPlayer()->forceSetHeadVecKeepSide(-getAirGravityVec());

    return true;
}

bool MarioSwim::checkWaterCube(bool checkOnly) {
    mUpVec = -getPlayer()->getAirGravityVec();
    WaterInfo bodyWaterInfo;
    MR::getWaterAreaObj(&bodyWaterInfo, getTrans());
    if ((getPlayer()->mMovementStates.jumping & 1) && getPlayer()->isRising()) {
        bodyWaterInfo.clear();
    }

    WaterInfo headWaterInfo;
    if (mSwimState == SWIM_STATE_NONE) {
        if (bodyWaterInfo.isInWater()) {
            getPlayer()->mDrawStates.inWater = true;
            getPlayer()->touchWater();

            MR::getWaterAreaInfo(&bodyWaterInfo, getTrans(), getPlayer()->getAirGravityVec(), false);

            getPlayer()->mRippleHeight = bodyWaterInfo.mCamWaterDepth;
            getPlayer()->mRippleRotate = bodyWaterInfo.mSurfaceNormal;
        }

        MR::getWaterAreaObj(&headWaterInfo, getTrans() + mUpVec * 110.0f);
        if (!bodyWaterInfo.isInWater() || !headWaterInfo.isInWater()) {
            return false;
        }
    } else {
        MR::getWaterAreaObj(&headWaterInfo, getTrans() + mUpVec * 80.0f);
    }

    if (bodyWaterInfo.isInWater()) {
        if (headWaterInfo.isInWater()) {
            mSwimState = SWIM_STATE_UNDERWATER;
            mWaterInfo = bodyWaterInfo;
        } else {
            if (getPlayer()->mVerticalSpeed - mWaterDepth < 80.0f) {
                if (mSwimState != SWIM_STATE_NONE) {
                    const Vec3f& shadowNorm = getPlayer()->getShadowNorm();
                    if (calcAngleD(shadowNorm) >= 30.0f) {
                        mNextAction = EXIT_ACTION_SLIDE;
                    } else {
                        mNextAction = EXIT_ACTION_JUMP;
                    }
                }

                mSwimState = SWIM_STATE_NONE;
                return false;
            }

            mSwimState = SWIM_STATE_ENTERING;
            mWaterInfo = bodyWaterInfo;
        }

        Vec2f depthVec;
        getWaterAreaInfo(&bodyWaterInfo, getTrans(), &depthVec);
        mWaterDepth = depthVec.x;
        mDistToFloor = depthVec.y;
        mSurfacePos = bodyWaterInfo.mSurfaceNormal;
        mSurfaceNorm = bodyWaterInfo.mSurfacePos;
        return true;
    } else {
        if ((getPlayer()->mMovementStates.jumping & 1) && !getPlayer()->isRising()) {
            headWaterInfo.clear();
        }

        if (headWaterInfo.isInWater()) {
            Vec2f depthVec;
            getWaterAreaInfo(&headWaterInfo, getTrans(), &depthVec);
            mSwimState = SWIM_STATE_SURFACE;
            mWaterDepth = depthVec.x;
            mDistToFloor = depthVec.y;
            mWaterInfo = headWaterInfo;

            mSurfacePos = bodyWaterInfo.mSurfaceNormal;
            mSurfaceNorm = bodyWaterInfo.mSurfacePos;

            getPlayer()->touchWater();
            return true;
        }
    }

    if (!checkOnly) {
        if (mWaterDepth > 100.0f && getPlayer()->mVerticalSpeed < 200.0f) {
            return true;
        }
    }

    mSwimState = SWIM_STATE_NONE;
    mNextAction = EXIT_ACTION_FALL;

    if (mWaterInfo.isInWater()) {
        Vec2f depthVec;
        getWaterAreaInfo(&mWaterInfo, getTrans(), &depthVec);
        mWaterDepth = depthVec.x;
    }

    if (!(mWaterDepth < -500.0f)) {
        if (mIsOnSurface && getPlayer()->mVerticalSpeed < 30.0f) {
            mNextAction = EXIT_ACTION_SLIDE;
        } else if (!MR::isStageSwimAngleLimit()) {
            mNextAction = EXIT_ACTION_SLIDE;
        }
    }

    return false;
}

void MarioSwim::updateUnderWater() {
    if (MarioModule::getPlayerMode() != 4 && MarioModule::getStickP() > 0.1f) {
        mWaterDistanceTarget = 0.0f;
    }

    MarioActor* actor;
    u32 searchLimit = 2;
    if (MarioModule::getPlayerMode() == 3) {
        searchLimit = 10;
    }

    Vec3f searchDir = MarioModule::getFrontVec();
    if (!MR::isNearZero(mActor->getLastMove())) {
        searchDir = mActor->getLastMove();
        MR::vecKillElement(searchDir, getGravityVec(), &searchDir);
        MR::normalizeOrZero(&searchDir);
    }

    const f32 kOffset = 100.0f;

    for (u32 i = 0; i < searchLimit; i++) {
        Vec3f checkPos = MarioModule::getTrans() + searchDir * kOffset + getGravityVec() * (mWaterDistanceTarget - kOffset);

        WaterInfo waterInfo;
        MR::getWaterAreaObj(&waterInfo, checkPos);

        if (waterInfo.isInWater()) {
            mSurfaceDepth = mWaterDistanceTarget;

            if (mWaterDistanceTarget > 300.0f) {
                getWaterAreaInfo(&waterInfo, checkPos, nullptr);
                Vec3f surfacePos = waterInfo.mSurfacePos;
                mSurfaceDepth = (surfacePos - getTrans()).dot(getGravityVec());
                mDeepWater = true;
            }

            else if (mWaterDistanceTarget < 210.0f && MarioModule::getPlayerMode() == 3) {
                if (!MR::checkStrikePointToMap(checkPos, nullptr)) {
                    getWaterAreaInfo(&waterInfo, getTrans(), nullptr);
                    Vec3f surfacePos = waterInfo.mSurfacePos;

                    if (waterInfo.mCamWaterDepth < 100.0f) {
                        if (!(getPlayer()->mMovementStates.onGround) || getPlayer()->mTargetWalkSpeedIndex) {
                            actor = mActor;
                            actor->createIceFloor(surfacePos + searchDir * 170.0f);
                        }

                        mWaterDistanceTarget = 0.0f;
                    }

                    return;
                }

                mWaterDistanceTarget = 0.0f;
            }

            mWaterDistanceTarget = 0.0f;
            return;
        }

        if (MR::checkStrikePointToMap(checkPos, nullptr)) {
            mDeepWater = false;
            mWaterDistanceTarget = 0.0f;
            return;
        }

        mWaterDistanceTarget += 40.0f;
    }
}

f32 MarioSwim::checkUnderWaterFull(const Vec3f& rVec) {
    for (f32 currentDist = 0.0f; currentDist < 10000.0f; currentDist += 40.0f) {
        Vec3f checkPos = getTrans() + rVec * 100.0f + getGravityVec() * (currentDist - 100.0f);
        if (MR::getAreaObj("ForbidWaterSearchCube", checkPos) != nullptr) {
            return -1.0f;
        }

        WaterInfo waterInfo;
        MR::getWaterAreaObj(&waterInfo, checkPos);
        if (waterInfo.isInWater()) {
            return currentDist;
        }

        if (MR::checkStrikePointToMap(checkPos, nullptr)) {
            return -1.0f;
        }
    }

    return -1.0f;
}

bool MarioSwim::checkWaterBottom() {
    Vec3f rayStart(getTrans() + mUpVec * 50.0f);

    u32 hitCount = MR::getNearPolyOnLineSort(rayStart, rayStart, -mUpVec * 2000.0f, nullptr);
    for (u32 i = 0; i < hitCount; i++) {
        Triangle tri;
        Vec3f hitPos;

        MR::getSortedPoly(&hitPos, &tri, i);

        if (!MR::isWaterPolygon(&tri)) {
            mDistToFloor = (getTrans() - hitPos).dot(mUpVec);

            return mDistToFloor > 200.0f;
        }
    }

    return true;
}

bool MarioSwim::getWaterAreaInfo(WaterInfo* pInfo, const Vec3f& rPos, Vec2f* pOutVec) {
    bool isInWater = MR::getWaterAreaInfo(pInfo, rPos, getPlayer()->getAirGravityVec(), false);

    if (pOutVec != nullptr) {
        pOutVec->x = -pInfo->mCamWaterDepth;
        pOutVec->y = pInfo->mDepth;
    }

    if (mIsOnSurface) {
        if (mSurfaceOffset > 0.0f) {
            f32 waveHeight = pInfo->mWaveHeight;
            mSurfaceOffset = 0.97f * mSurfaceOffset + 0.03f * waveHeight;
        } else {
            f32 waveHeight = pInfo->mWaveHeight;
            mSurfaceOffset = 0.99f * mSurfaceOffset + 0.01f * (waveHeight * 0.5f);
        }
    } else {
        mSurfaceOffset = 0.8f * mSurfaceOffset;
    }

    mWaterEdgePos = pInfo->mEdgePos;
    mDistanceToWaterSurface = pInfo->mEdgeDistance;
    Vec3f currentFlow(pInfo->mStreamVec);

    f32 depthFactor = 1.0f;
    if (mWaterDepth > -500.0f) {
        depthFactor = -mWaterDepth / 500.0f;
    }

    f32 finalScale = MR::clamp(depthFactor, 0.0f, 1.0f);
    if (mIsOnSurface) {
        finalScale = 0.0f;
    }

    currentFlow.scale(finalScale);
    mWaterStreamVel = mWaterStreamVel * 0.95f + currentFlow * 0.05f;
    mWaterStreamDir = currentFlow;

    return isInWater;
}

const Vec3f& MarioSwim::getGravityVec() const {
    return MarioModule::getGravityVec();
}

bool MarioSwim::surfacePaddle() {
    if (mDashTimer != 0) {
        return false;
    }

    stopAnimation(nullptr);
    changeAnimation("水上一掻き");
    // Surface Paddle
    playSound("水面一掻き");
    mDashTimer = 40;

    return true;
}

void MarioSwim::spin() {
    if (mJetTimer != 0) {
        if (mActor->isRequestSpin()) {
            if (mSpinTimer == 0 && mSpinDashTimer == 0 && mJetCooldownTimer == 0) {
                dropJet(false);
            }
        }

        return;
    }

    if (mKnockbackTimer != 0) {
        return;
    }

    if (mZSinkTimer != 0) {
        return;
    } else if (checkTrgA()) {
        if (mSpinTimer != 0) {
            mSpinTimer = 0;
            startSpinDash();
            return;
        }
    }

    if (mSpinTimer != 0) {
        mSpinTimer--;
        mForwardSpeed = 0.95f * mForwardSpeed;
        if (mSpinTimer == 0) {
            startSpinDash();
        }
    }

    if (mSpinDashTimer != 0) {
        mSpinDashTimer--;
    }

    if (!mActor->isRequestSpin()) {
        return;
    }

    if (mSpinTimer != 0) {
        return;
    }

    if (mSpinDashTimer != 0) {
        return;
    }

    if (mSpinDashTimer != 0) {
        return;
    }

    if (mSpinTimer != 0) {
        mSpinTimer = mActor->getConst().getTable()->mSwimSpinMissOfsTime;
        mForwardSpeed = 0.5f * mForwardSpeed;

    } else {
        mSpinTimer = 1;
    }

    mDashTimer = 0;
    mSpinDashTimer = 0;
    mSpinPunchHit = false;

    stopAnimationUpper(nullptr);
    if (!mIsOnSurface) {
        changeAnimation("水泳スピン");
        playSound("水中スピン開始");
    } else {
        changeAnimation("水上スピン");
        playSound("水面スピン開始");
    }

    mActor->setPunchHitTimer(40);
    playEffect("水泳スピン");
}

void MarioSwim::startSpinDash() {
    stopEffect("水泳スピン");

    if (mSpinPunchHit) {
        stopAnimation("水泳スピン");
        stopAnimation("水上スピン");
        return;
    }

    mSpinDashTimer = mActor->getConst().getTable()->mSwimDashTime;
    mActionLockTimer = mActor->getConst().getTable()->mSwimSpinTime;

    if (mIsOnSurface) {
        mDashTimer = 20;
        mSwimYAccTimer = 50;
        mSpinDashTimer = 45;
        mActionLockTimer = 30;
    } else {
        mDashTimer = mActor->getConst().getTable()->mSwimSpinFrontTime;
        mSwimYAccTimer = mActor->getConst().getTable()->mSwimSpinYspeedTime;
    }

    if (!mIsOnSurface) {
        changeAnimation("水泳スピン移動");
    } else {
        changeAnimation("水上スピン移動");
    }

    mActor->setPunchHitTimer(mSpinDashTimer);

    if (gIsLuigi) {
        decOxygen(mActor->getConst().getTable()->mOxygenMax / 16);
    }
}

void MarioSwim::hitPunch(const Vec3f& rPunchDir) {
    if (mSpinTimer != 0) {
        mKnockbackTimer = 30;
        mForwardSpeed = 0.0f;
        mKnockbackVel = rPunchDir;
        mSpinPunchHit = true;

        stopAnimation("アッパーパンチ");
    }
}

void MarioSwim::jet() {
    if (mActor->getCarrySensor() == nullptr) {
        mJetTimer = 0;
        mJetCooldownTimer = 0;
    }

    if (mJetCooldownTimer != 0) {
        mJetCooldownTimer--;
    }

    if (mJetTimer != 0) {
        MR::setStageBGMStateBit(4);

        f32 accFactor = calcRingAcc();
        f32 targetSpeed;

        if (accFactor != 1.0f) {
            targetSpeed = accFactor * mActor->getConst().getTable()->mSwimFrontMaxSpeed;
            switch (mJetKind) {
            case 0:
                if (targetSpeed < mActor->getConst().getTable()->mSwimFrontJetSpeed) {
                    targetSpeed = mActor->getConst().getTable()->mSwimFrontJetSpeed;
                }

                break;
            default:
                if (targetSpeed < mActor->getConst().getTable()->mSwimFrontJetSpeedSlow) {
                    targetSpeed = mActor->getConst().getTable()->mSwimFrontJetSpeedSlow;
                }

                break;
            }
        } else {
            switch (mJetKind) {
            case 0:
                targetSpeed = mActor->getConst().getTable()->mSwimFrontJetSpeed;
                break;
            default:
                targetSpeed = mActor->getConst().getTable()->mSwimFrontJetSpeedSlow;
                break;
            }
        }

        if (mJetTimer == 1) {
            if (checkLvlZ()) {
                MR::emitEffect(mActor->getCarrySensor()->mHost, "BrakeLamp");
                getPlayer()->mHitStates.jet = 1;
            }
        }

        mJetTimer++;

        if (checkLvlZ()) {
            if (checkTrgZ() || mStateTimer < 2) {
                playSound("亀ブレーキ");
                MR::emitEffect(mActor->getCarrySensor()->mHost, "BrakeLamp");
            }

            getPlayer()->mHitStates.jet = 1;

            if (mForwardSpeed > targetSpeed) {
                mForwardSpeed *= 0.95f;
                return;
            }

            if (mForwardSpeed > (0.1f * targetSpeed)) {
                mForwardSpeed *= 0.96f;
                return;
            }

        } else {
            playSound("亀ジェット泳ぎ");
            MR::forceDeleteEffect(mActor->getCarrySensor()->mHost, "BrakeLamp");
        }

        if (mForwardSpeed < targetSpeed) {
            if (mForwardSpeed < 1.0f) {
                mForwardSpeed = 1.0f;
            }

            mForwardSpeed *= 1.02f;

            if (mRingDashTimer != 0) {
                mForwardSpeed *= 1.5f;
            }
        } else {
            if (mForwardSpeed > targetSpeed) {
                mForwardSpeed *= 0.98f;
            } else {
                mForwardSpeed = targetSpeed;
            }
        }
    } else {
        ringDash();
    }
}

void MarioSwim::resetJet() {
    if (mJetTimer != 0) {
        if (isStatusActiveS(this)) {
            getAnimator()->changeDefault("水泳基本");
        }
    }

    mJetTimer = 0;
}

void MarioSwim::dropJet(bool isDamage) {
    if (mJetTimer == 0) {
        return;
    }

    mJetTimer = 0;

    if (mForwardSpeed > mActor->getConst().getTable()->mSwimFrontMaxSpeed) {
        mForwardSpeed = mActor->getConst().getTable()->mSwimFrontMaxSpeed;
    }

    if (isDamage) {
        changeAnimation("水泳ジェット終了", "水泳基本");
    } else {
        changeAnimation("水泳亀投げ", "水泳基本");
    }

    if (mActor->getCarrySensor()) {
        LiveActor* shell = mActor->getCarrySensor()->mHost;
        MR::forceDeleteEffect(shell, "BrakeLamp");
    }

    if (isDamage) {
        mActor->damageDropThrowMemoSensor();
    } else {
        mActor->releaseThrowMemoSensor();
    }

    mKnockbackTimer = 15;
    mKnockbackVel.zero();

    startPadVib(static_cast< u32 >(0));
}

bool MarioSwim::startJet(u32 type) {
    if (mJetTimer != 0) {
        return false;
    }

    mJetKind = type;
    mJetTimer = 1;

    forceStopSpin();
    // Swim Jet
    if (!isAnimationRun("水泳ジェット")) {
        // Swim Jet Start
        changeAnimation("水泳ジェット開始", "水泳ジェット");
    } else {
        changeAnimation(nullptr, "水泳ジェット");
    }

    mJetCooldownTimer = 20;
    return true;
}

void MarioSwim::forceStopSpin() {
    mSpinDashTimer = 0;
    mSpinTimer = 0;
    setYangleOffset(0.0f);

    // Swim Spin
    stopAnimation("水泳スピン");
    stopAnimation("水泳スピン移動");
    // Surface Spin
    stopAnimation("水上スピン");
    stopAnimation("水上スピン移動");

    stopEffect("水泳スピン");
}

void MarioSwim::addFaint(const Vec3f& rFaintDir) {
    if (getPlayer()->getDamageAfterTimer() != 0) {
        return;
    }

    if (mDamageType == 0) {
        Mario* player = getPlayer();
        player->mDamageAfterTimer = 150;
    }

    dropJet(false);
    mKnockbackTimer = mActor->getConst().getTable()->mWaterInnerFaintTime;

    changeAnimation("水泳ダメージ");
    playSound("水中ダメージ");
    playSound("声水中ダメージ");
    playSound("ダメージ");

    startPadVib(2);
    mKnockbackVel = rFaintDir;
    mKnockbackVel.setLength(mActor->getConst().getTable()->mSwimFaintSpeed);
    mActionLockTimer = 0;
    forceStopSpin();
    mForwardSpeed = 0.0f;

    if (mDamageType == 0) {
        mActor->decLife(0);
        if (mActor->mHealth == 0) {
            mActor->forceGameOverNonStop();
        }
    }

    mDamageType = 0;
}

void MarioSwim::addDamage(const Vec3f& rDamageDir) {
    Vec3f surfaceKnockback;
    Vec3f postureDir;
    if (getPlayer()->getDamageAfterTimer() != 0) {
        return;
    }

    if (mDamageType == 0) {
        Mario* player = getPlayer();
        player->mDamageAfterTimer = 180;
    }

    dropJet(false);
    mForwardSpeed = 0.0f;

    if (mDamageType == 0) {
        playSound("ダメージ");
    } else {
        playSound("壁衝突");
    }

    if (mIsOnSurface) {
        MR::vecKillElement(rDamageDir, mUpVec, &surfaceKnockback);
        surfaceKnockback.setLength(mActor->getConst().getTable()->mSwimSurfaceDamageSpeed);

        Vec3f jumpVel = mUpVec * mActor->getConst().getTable()->mWaterSurfaceDamageJump + surfaceKnockback;
        getPlayer()->tryForcePowerJump(jumpVel, false);
        changeAnimationNonStop("水上ダメージ中");
        playSound("水面ダメージ");
        playSound("声小ダメージ");

        mFrontVec = -surfaceKnockback;
        mNextAction = EXIT_ACTION_POWER_JUMP;
        getPlayer()->closeStatus(this);

        if (mDamageType == 0) {
            mActor->decLife(0);
            if (mActor->mHealth == 0) {
                mActor->forceGameOverNonStop();
            }
        }

        mDamageType = 0;
    } else {
        mKnockbackTimer = mActor->getConst().getTable()->mWaterInnerDamageTime;
        changeAnimation("水泳ダメージ中");

        mKnockbackVel = rDamageDir;
        mKnockbackVel.setLength(mActor->getConst().getTable()->mSwimDamageSpeed);

        playSound("水中ダメージ");
        playSound("声水中ダメージ");

        if (mDamageType == 0) {
            decLife();
        }

        mDamageType = 0;
    }

    if (mLargeDamage != 0) {
        mKnockbackVel.setLength(mActor->getConst().getTable()->mSwimDamageSpeedEx1);
        mKnockbackTimer = mActor->getConst().getTable()->mSwimDamageTimerEx1;
        mLargeDamage = 0;
    }

    startPadVib(3);
    playEffect("ダメージ");
    mActionLockTimer = 0;
    forceStopSpin();
    mForwardSpeed = 0.0f;

    MR::vecKillElement(rDamageDir, getGravityVec(), &postureDir);

    if (MR::normalizeOrZero(&postureDir) == 0) {
        mFrontVec = -postureDir;
    }
}

void MarioSwim::updateTilt() {
    f32 targetTiltX = 0.0f;
    f32 blendX = 0.1f;

    if (mIsOnSurface) {
        if (getStickP() > 0.1f) {
            f32 stickX = getStickX();

            f32 angle = (stickX * FloatUtil::PI()) / 6.0f;
            f32 absAngle = MR::abs(angle);

            Vec3f padDir = getWorldPadDir();
            f32 dot = mFrontVec.dot(padDir);
            f32 scale = MR::clamp(1.0f - dot, 0.0f, 1.0f);

            targetTiltX = absAngle * scale;

            Vec3f cross = mFrontVec.cross(padDir);
            if (cross.dot(mUpVec) > 0.0f) {
                targetTiltX = -targetTiltX;
            }

            if (MR::abs(mCurrentTiltX) < MR::abs(targetTiltX)) {
                blendX = 0.1f;
            } else {
                blendX = 0.03f;
            }
        }
    } else {
        targetTiltX = (getStickX() * FloatUtil::PI()) / 5.0f;
    }

    if (isAnimationRun("水泳ターン左") || isAnimationRun("水泳ターン右") || isAnimationRun("水泳ターン下")) {
        f32 stickX = getStickX();
        targetTiltX = (MR::clamp(stickX / 0.7f, -1.0f, 1.0f) * FloatUtil::PI()) / 8.0f;
    }

    f32 targetTiltY = 0.0f;

    if (!mIsOnSurface) {
        if (mIdleWaitTimer == 0) {
            f32 scaledTilt = (getStickY() * FloatUtil::PI());
            targetTiltY = scaledTilt / 12.0f;

            if (getStickY() > 0.0f && mVerticalAngle <= 2.0f / 3.0f * FloatUtil::PI()) {
                targetTiltY = getStickY() * FloatUtil::PI() / 8.0f;
            }
        } else if (getStickY() < 0.0f) {
            const MarioConstTable* table = mActor->getConst().getTable();
            targetTiltY = (getStickY() * FloatUtil::PI()) / table->mSwimTiltZup;
        } else if (getStickY() > 0.0f) {
            const MarioConstTable* table = mActor->getConst().getTable();
            targetTiltY = (getStickY() * FloatUtil::PI()) / table->mSwimTiltZdown;
        } else if (mIdleWaitTimer != 0) {
            f32 fade = static_cast< f32 >(mIdleWaitTimer) / 120.0f;
            if (fade > 1.0f) {
                fade = 1.0f;
            }

            targetTiltY = targetTiltY - (fade * (FloatUtil::PI() / 10.0f));
        } else {
            targetTiltY = 0.0f;
        }
    }

    mCurrentTiltX = (blendX * targetTiltX) + ((1.0f - blendX) * mCurrentTiltX);
    mCurrentTiltY = (0.1f * targetTiltY) + (0.9f * mCurrentTiltY);

    Mtx mtxRotX, mtxRotZ;
    NebulaMtxRotRad(mtxRotX, 'X', mCurrentTiltX);
    NebulaMtxRotRad(mtxRotZ, 'Z', mCurrentTiltY);
    MR::multMtx(mUpperBodyJointMtx, mtxRotZ, mtxRotX);

    u32 jointID = getAnimator()->getUpperJointID();
    setJointGlobalMtx(jointID, mUpperBodyJointMtx);
}

void MarioSwim::pushedByWaterWall() {
    if (!(mDistanceToWaterSurface < 0.0f)) {
        f32 wallDistanceLimit = 80.0f;
        if (mDistanceToWaterSurface < wallDistanceLimit) {
            Vec3f pushDir = getTrans() - mWaterEdgePos;
            pushDir.setLength(wallDistanceLimit - mDistanceToWaterSurface);
            getPlayer()->push(pushDir);

            if (MR::normalizeOrZero(&pushDir) == 0) {
                const Vec3f& headVec = getPlayer()->mHeadVec;
                f32 dot = pushDir.dot(-headVec);

                if (mWaterWallTimer != 0) {
                    dot = 1.0f;
                }

                if (!(dot < 0.0f)) {
                    if (dot < 0.995f) {
                        Vec3f newHead;
                        MR::vecKillElement(headVec, pushDir, &newHead);
                        MR::normalize(&newHead);

                        MR::vecBlendSphere(getPlayer()->mHeadVec, newHead, &newHead, 0.2f);
                        getPlayer()->forceSetHeadVecKeepSide(newHead);
                        MR::vecKillElement(newHead, getGravityVec(), &mFrontVec);

                        Mario* player = getPlayer();
                        mVerticalAngle = MR::diffAngleAbs(player->mHeadVec, -getGravityVec());

                    } else {
                        MR::vecBlendSphere(mFrontVec, pushDir, &mFrontVec, 0.2f);
                        mWaterWallTimer = 30;
                    }
                }
            }
        }
    }
}

bool MarioSwim::tryJetAttack(HitSensor* pHitSensor) {
    if (mActor->tryJetAttack(pHitSensor) == true) {
        dropJet(1);

        const Vec3f& trans = getTrans();
        Vec3f damageDir = trans - pHitSensor->mPosition;
        addDamage(damageDir);

        return true;
    } else {
        return false;
    }
}

void MarioSwim::hitHead(const HitInfo* pHit) {
    if (mJetTimer != 0) {
        if (sendPunch(pHit->mParentTriangle.mSensor, false) == 1) {
            startCamVib(0);
            return;
        }

        if (tryJetAttack(pHit->mParentTriangle.mSensor)) {
            return;
        }

        const Vec3f* normal = pHit->mParentTriangle.getNormal(0);
        if (normal->dot(getGravityVec()) > 0.0f) {
            Mario* player = getPlayer();
            Vec3f direction;
            Vec3f velocityPart;

            f32 element = MR::vecKillElement(player->mPostureUpVec, -*normal, &velocityPart);
            velocityPart += *normal * element * 1.5f;

            if (velocityPart.length() < 10.0f) {
                velocityPart.setLength(10.0f);
            }

            MR::vecKillElement(velocityPart, getGravityVec(), &direction);

            direction.length();
            if (MR::normalizeOrZero(&direction) == false) {
                mFrontVec = direction;
            }
        }
    }

    Vec3f pushBack = getTrans() - pHit->mHitPos;
    MR::vecKillElement(pushBack, *pHit->mParentTriangle.getNormal(0), &pushBack);
    pushBack.setLength(5.0f);
    addVelocity(pushBack);
}

void MarioSwim::hitPoly(u8 type, const Vec3f& rNormal, HitSensor* pSensor) {
    if (mStateTimer <= 120) {
        return;
    }

    if (!mIsOnSurface) {
        return;
    }

    const Vec3f& gravity = getPlayer()->getAirGravityVec();
    f32 dot = rNormal.dot(gravity);
    f32 invDot = -dot;

    const Vec3f& swimGravity = getAirGravityVec();
    Vec3f negNormal = -rNormal;

    f32 angle = MR::diffAngleAbsHorizontal(negNormal, mFrontVec, swimGravity);
    if (invDot >= 0.5f && angle <= PI / 4) {
        mSlideOut = true;
    }
}

void MarioSwim::hitWall(const Vec3f& rNormal, HitSensor* pSensor) {
    if (mActor->mTakenSensor == 0) {
        f32 floorDot = -rNormal.dot(getPlayer()->getAirGravityVec());

        if (!mIsOnSurface && floorDot > 0.70700002f) {
            mWallHit = true;
            return;
        } else if (mIsOnSurface) {
            if (MR::diffAngleAbsHorizontal(-rNormal, mFrontVec, getAirGravityVec()) < DEG_TO_RAD(45)) {
                mWallStickTimer = 6;
            }

            if (mStateTimer > 0x78) {
                // If on surface and hitting something slope-like, treat as slide/floor interaction
                if (floorDot >= 0.5f) {
                    mSlideOut = true;
                    return;
                }
            } else {
                return;
            }
        }

        bool isHit = false;

        if (!mIsOnSurface && mForwardSpeed > 1.0f) {
            Vec3f projectedVelocity;
            MR::vecKillElement(getPlayer()->mPostureUpVec, rNormal, &projectedVelocity);

            if (projectedVelocity.dot(mUpVec) > 0.0f) {
                if (getStickY() <= 0.0f) {
                    mVerticalAngle -= 0.1f;
                }
            } else {
                if (getStickY() >= 0.0f) {
                    mVerticalAngle += 0.1f;
                }
            }
        }

        mWallStickTimer = 6;
        mDashTimer = 0;

        if (mJetTimer != 0 || (!MR::isNearZero(mWaterStreamDir) && mActionLockTimer != 0)) {
            bool punched = false;
            if (mJetTimer != 0) {
                punched = sendPunch(pSensor, false);
            }

            if (!punched) {
                if (mJetTimer != 0) {
                    if (tryJetAttack(pSensor)) {
                        return;
                    }
                }

                Vec3f velocityAfterHit;
                Vec3f tangent;
                Mario* player = getPlayer();
                f32 speedIntoWall = MR::vecKillElement(player->mPostureUpVec, -rNormal, &tangent);

                tangent += rNormal * speedIntoWall * 1.5f;
                MR::vecKillElement(tangent, getGravityVec(), &velocityAfterHit);

                bool isReflect = true;
                f32 speedReduction = mActor->getConst().getTable()->mTurboReductionHitWall;

                if (!MR::normalizeOrZero(&velocityAfterHit)) {
                    if (rNormal.dot(getGravityVec()) > 0.1f && mVerticalAngle < 1.5707964f) {
                        mVerticalAngle = PI - mVerticalAngle;
                    }

                    mKnockbackTimer = 20;
                    mKnockbackVel = rNormal * 6.0f;

                    if (MR::isNearZero(mWaterStreamDir)) {
                        f32 impactAngle = MR::diffAngleAbsHorizontal(mFrontVec, rNormal, getAirGravityVec());

                        if (impactAngle >= 2.6179941f) {
                            f32 headAngle = MR::diffAngleAbs(getPlayer()->mPostureUpVec, rNormal);

                            if (headAngle <= 2.6179941f) {
                                if (headAngle >= 2 * PI) {
                                    Mario* player = getPlayer();
                                    Vec3f cross1 = rNormal.cross(player->mPostureUpVec);
                                    MR::normalizeOrZero(&cross1);

                                    Vec3f cross2 = getPlayer()->mPostureUpVec.cross(cross1);
                                    MR::normalizeOrZero(&cross2);

                                    getPlayer()->forceSetHeadVecKeepSide(cross2);

                                    player = getPlayer();
                                    mVerticalAngle = MR::diffAngleAbs(player->mHeadVec, -getAirGravityVec());
                                }

                                isReflect = false;

                            } else {
                                mKnockbackTimer = 60;
                                mKnockbackVel = rNormal * 16.0f;
                            }
                        } else {
                            Vec3f glanceDir;
                            MR::vecKillElement(mFrontVec, rNormal, &glanceDir);
                            if (!MR::normalizeOrZero(&glanceDir)) {
                                mFrontVec = glanceDir;
                                speedReduction = 0.8f;
                                isReflect = false;
                            }
                        }
                    } else {
                        if (velocityAfterHit.dot(mWaterStreamDir) > 0.0f) {
                            mFrontVec = velocityAfterHit;
                        } else {
                            mKnockbackTimer = 60;
                            mKnockbackVel = rNormal * 16.0f;
                        }
                    }
                }

                if (mJetTimer != 0 && isReflect) {
                    playEffectTrans("壁ヒット", getPlayer()->getWallPos());
                    playSound("亀壁ヒット");
                    changeAnimation("水泳ジェット壁ターン");
                }

                mForwardSpeed *= speedReduction;
            } else {
                startCamVib(0);
            }

            isHit = true;
        }

        // Spin Attack Collision
        if (mSpinDashTimer != 0) {
            if (!sendPunch(pSensor, true)) {
                mForwardSpeed = 0.0f;
                forceStopSpin();
                changeAnimation("水泳壁ヒット");
                playEffectTrans("壁ヒット", getPlayer()->getWallPos());
                startCamVib(0);
                mKnockbackTimer = 60;
                mKnockbackVel = rNormal * 6.0f;
            }

            isHit = true;
        } else if (mSpinTimer != 0) {
            if (!sendPunch(pSensor, false)) {
                mSpinTimer = 0;
                mForwardSpeed = 0.0f;
                playEffectTrans("壁ヒット", getPlayer()->getWallPos());
                startCamVib(2);
                mKnockbackTimer = 30;
                mKnockbackVel = rNormal * 6.0f;
            }

            isHit = true;
        }

        // Ring Dash Collision
        if (mRingDashTimer != 0) {
            if (mJetTimer != 0) {
                dropJet(true);
            }

            mForwardSpeed = 0.0f;
            changeAnimation("水泳壁ヒット");
            playEffectTrans("壁ヒット", getPlayer()->getWallPos());
            startCamVib(0);
            mKnockbackTimer = 60;
            mKnockbackVel = rNormal * 6.0f;
            isHit = true;
        }

        mRingDashTimer = 0;
        mRingDashChargeTimer = 0;

        if (isHit) {
            startPadVib(2);
        }

        // Wall Push / Kick Logic check
        if (rNormal.dot(getGravityVec()) > 0.8f) {
            Vec3f* worldPadDir = &getWorldPadDir();
            if (getStickP() != 0.0f) {
                if (checkTrgA()) {
                    if (mKnockbackTimer == 0) {
                        mKnockbackTimer = 60;
                    }

                    mKnockbackVel = *worldPadDir * 10.0f + getGravityVec() * 10.0f;
                }
            }
        }

        stopEffect("水泳スピン");
    }
}

void MarioSwim::decLife() {
    mDrownTimer = 0;
    mActor->decLife(0);
    if (mActor->mHealth == 0) {
        mActor->forceGameOver();

        Vec3f gravityVec = -getGravityVec();
        getPlayer()->forceSetHeadVecKeepSide(gravityVec);

        u32 upperJoint = getAnimator()->getUpperJointID();
        setJointGlobalMtx(upperJoint, nullptr);
    }
}

void MarioSwim::incLife() {
    mDrownTimer = 0;
    mActor->incLife(1);
    incOxygen();
}

void MarioSwim::incOxygen() {
    u32 oxygenMax;
    if (mOxygen == (oxygenMax = mActor->getConst().getTable()->mOxygenMax)) {
        return;
    }

    mOxygen += (oxygenMax / 8);

    if (mOxygen > (oxygenMax = mActor->getConst().getTable()->mOxygenMax)) {
        mOxygen = oxygenMax;
    }

    if (mOxygen == mActor->getConst().getTable()->mOxygenMax) {
        // Play "Full" sound
        playSound("酸素完全回復");
    } else {
        // Play "Recovering" sound
        playSound("酸素回復");
    }

    mOxygenWarningTimer = 120;
    mOxygenDecreaseDelay = 120;
}

void MarioSwim::decOxygen(u16 amount) {
    if (mOxygen == 0) {
        return;
    }

    MarioActor* actor = mActor;

    if (actor->mInRush != 0) {
        return;
    }

    if (actor->mInDemo != 0) {
        return;
    }

    if (isStatusActiveID(MarioStatus_Talk)) {
        return;
    }

    if (mOxygen < amount) {
        mOxygen = 0;
    } else {
        mOxygen -= amount;
    }

    if (mOxygen == 0) {
        playSound("無酸素警告");
    }
}

void MarioSwim::draw3D() const {
    WaterInfo camWaterInfo;
    MR::getWaterAreaObj(&camWaterInfo, getCamPos());
    if (camWaterInfo.isInWater()) {
        mMarineSnow->view();
    } else {
        mMarineSnow->clear();
    }

    mMarineSnow->draw(getTrans(), mUpVec, -mWaterDepth);
}

void MarioSwim::onSurface() {
    if (!mIsOnSurface) {
        mIsOnSurface = true;
    }
}

void MarioSwim::updateLifeByTime() {
    updateOxygenWatch();

    if (mActor->mInDemo) {
        mColdWaterDamageInterval = 120;
    } else {
        const WaterArea* waterArea = mWaterInfo.mWaterArea;
        s32 objArg = -1;
        bool isColdWater = false;
        if (waterArea != nullptr) {
            objArg = MR::getAreaObjArg(waterArea, 1);
        }

        if (objArg == 1) {
            isColdWater = true;
        }

        if (isColdWater) {
            if (getPlayer()->isDamaging()) {
                mColdWaterDamageInterval = 120;
            }

            getPlayer()->mHitStates.swimLifeDrain = 1;

            if (!mColdWaterDamageInterval) {
                mColdWaterDamageInterval = 120;
            } else {
                mColdWaterDamageInterval--;
                if (mColdWaterDamageInterval == 0) {
                    doDecLifeByCold();
                    mColdWaterDamageInterval = 120;
                }
            }
        } else {
            mColdWaterDamageInterval = 0;
        }

        if (!isColdWater && mIsOnSurface) {
            mDrownTimer = 0;
            mOxygenDecreaseDelay = 120;

            if (mOxygen != mActor->getConst().getTable()->mOxygenMax) {
                mOxygen += 32;
                if (mOxygen > mActor->getConst().getTable()->mOxygenMax) {
                    playSound("水面酸素完全回復");
                    mOxygen = mActor->getConst().getTable()->mOxygenMax;
                } else {
                    playSound("水面酸素回復");
                }

                mOxygenWarningTimer = 180;
            }
        } else {
            if (mOxygen) {
                if (mOxygenDecreaseDelay) {
                    mOxygenDecreaseDelay--;
                } else {
                    decOxygen(1);
                }
            } else {
                mDrownTimer++;
                u32 damageInterval = mActor->getConst().getTable()->mWaterDecInterval;
                if (mDrownTimer >= damageInterval) {
                    doDecLifeByCold();
                }
            }
        }
    }
}

void MarioSwim::doDecLifeByCold() {
    if (mActor->mHealth != 0) {
        decLife();

        if (mJetTimer != 0) {
            changeAnimation("水泳ダメージ亀");
        } else {
            changeAnimation("水泳ダメージ");
        }

        playSound("水中ダメージ");
        playSound("声冷水ダメージ");
        playSound("ダメージ");
        startPadVib(2);

        mActor->mDamageFlashTimer = 16;
    }
}

bool MarioSwim::passRing(const HitSensor* pSensor) {
    const Vec3f& ringPos = pSensor->mPosition;
    if (mRingDashTimer == 0) {
        mRingDashChargeTimer = mActor->getConst().getTable()->mSwimRingDashChargeTime;

        if (mJetTimer == 0) {
            changeAnimation("リングダッシュ準備");
        } else {
            changeAnimation("カメ持ちリング準備");
        }
    }

    if (mRingDashChargeTimer != 0) {
        getPlayer()->push((ringPos - getTrans()) * 0.25f);
    }

    DashRing* host = static_cast< DashRing* >(pSensor->mHost);
    mRingDashMaxDuration = host->mMaxDuration;
    mRingDashSpeedScale = host->mSpeedScale;
    mRingDashTimer = host->mBoostTime;

    playSound("亀加速");

    return true;
}

f32 MarioSwim::calcRingAcc() {
    if (mRingDashTimer != 0) {
        if (mRingDashChargeTimer != 0) {
            mRingDashChargeTimer--;

            if (mRingDashChargeTimer == 1) {
                MarioActor* actor = mActor;
                MarioConstTable* table = actor->mConst->getTable();

                actor->mFlashFogTimer = table->mStarPieceFogTime;
                actor->mFlashFogRate = 1.0f;
                actor->mFlashFogColor.r = 0xFF;
                actor->mFlashFogColor.g = 0xFF;
                actor->mFlashFogColor.b = 0xFF;
                actor->mFlashFogColor.a = 0;
                actor->mFlashFogOn = 0;
            }

            if (mRingDashChargeTimer == 0) {
                startPadVib(3);
                if (mJetTimer == 0) {
                    changeAnimation("リングダッシュ");
                } else {
                    changeAnimation("カメ持ちリング");
                }
            }

            return 1.0f;
        } else {
            mBlurOffset += 0.5f;
            if (mBlurOffset > 30.0f) {
                mBlurOffset = 30.0f;
            }

            f32 result = mRingDashSpeedScale;
            if (mRingDashTimer < mRingDashMaxDuration) {
                f32 ratio = static_cast< f32 >(mRingDashTimer) / static_cast< f32 >(mRingDashMaxDuration);
                result = (1.0f - ratio) + (mRingDashSpeedScale * ratio);
            }

            mRingDashTimer--;
            return result;
        }
    } else {
        if (mBlurOffset > 0.0f) {
            mBlurOffset -= 0.5f;
        } else {
            mBlurOffset = 0.0f;
        }

        return 1.0f;
    }
}

void MarioSwim::ringDash() {
    f32 accFactor = calcRingAcc();

    if (mRingDashTimer != 0) {
        f32 targetSpeed = accFactor * mActor->getConst().getTable()->mSwimFrontMaxSpeed;

        if (mForwardSpeed < targetSpeed) {
            if (mForwardSpeed < 1.0f) {
                mForwardSpeed = 1.0f;
            }

            if (targetSpeed / mForwardSpeed > 1.5f) {
                mForwardSpeed *= 1.5f;
            } else {
                mForwardSpeed = targetSpeed;
            }
        } else {
            if (mForwardSpeed > targetSpeed) {
                if (targetSpeed / mForwardSpeed < 0.98f) {
                    mForwardSpeed *= 0.98f;
                } else {
                    mForwardSpeed = targetSpeed;
                }
            } else {
                mForwardSpeed = targetSpeed;
            }
        }
    }
}

void MarioSwim::resetAndFixPose() {
    mActionLockTimer = 0;
    mSinkTimer = 0;
    mSwimYAccTimer = 0;
    mZSinkTimer = 0;
    mIdleWaitTimer = 0;
    mBuoyancyDelayTimer = 0;
    mDashTimer = 0;
    mSpinDashTimer = 0;
    mSpinTimer = 0;

    mForwardSpeed = 0.0f;
    mStickInputYInertia = 0.0f;
    mStickInputXInertia = 0.0f;
    mBuoyancySpeed = 0.0f;

    MR::vecKillElement(getFrontVec(), getGravityVec(), &mFrontVec);

    Mario* player = getPlayer();
    mVerticalAngle = MR::diffAngleAbs(player->mHeadVec, -getGravityVec());
}

f32 MarioSwim::getStickY() const {
    f32 stickY = MarioModule::getStickY();

    if (mIsSinking && stickY < 0.0f) {
        if (stickY > -0.5f) {
            stickY = 0.0f;
        } else {
            stickY = (0.5f + stickY) / 0.5f;
        }
    }

    return stickY;
}

void MarioSwim::updateOxygenWatch() {
    if (mOxygen <= (mActor->getConst().getTable()->mOxygenMax / 4)) {
        mOxygenWarningTimer = 180;
    }

    if (mOxygen == (mActor->getConst().getTable()->mOxygenMax / 4)) {
        playSound("酸素減少警告");
    }

    if (mOxygen == (mActor->getConst().getTable()->mOxygenMax / 2)) {
        mOxygenWarningTimer = 180;
        playSound("酸素減少警告");
    }

    if (mOxygenWarningTimer != 0) {
        mOxygenWarningTimer--;
    }
}

void MarioSwim::addVelocity(const Vec3f& rVelocity) {
    if (mIsOnSurface && mNextAction != EXIT_ACTION_NONE) {
        const Vec3f& gravity = getAirGravityVec();
        Vec3f horizontalVel;
        f32 verticalMag = MR::vecKillElement(rVelocity, gravity, &horizontalVel);
        if (verticalMag < 0.0f) {
            MarioModule::addVelocity(horizontalVel);
        } else {
            MarioModule::addVelocity(rVelocity);
        }
    } else {
        MarioModule::addVelocity(rVelocity);
    }
}

void MarioSwim::addVelocity(const Vec3f& rVelocity, f32 scale) {
    if (mIsOnSurface && mNextAction != EXIT_ACTION_NONE && mWaterDepth >= 0.0f) {
        const Vec3f& gravity = getAirGravityVec();
        Vec3f horizontalVel;
        f32 verticalMag = MR::vecKillElement(rVelocity, gravity, &horizontalVel);

        if (verticalMag < 0.0f) {
            MarioModule::addVelocity(horizontalVel, scale);
        } else {
            MarioModule::addVelocity(rVelocity, scale);
        }
    } else {
        MarioModule::addVelocity(rVelocity, scale);
    }
}

void MarioSwim::doJetJump(u8 type) {
    if (!getPlayer()->mHitStates.jumpForbidden && mRingDashChargeTimer == 0) {
        f32 verticalScale;
        f32 horizontalSpeed = 12.0f;
        f32 verticalBoost = 15.0f;

        if (checkLvlA()) {
            horizontalSpeed = 18.0f;
            verticalBoost = 30.0f;
        } else if (type == 0) {
            getPlayer()->mActionStates.jetJumped = true;
        }

        const MarioConstTable* table = mActor->getConst().getTable();
        f32 speedRatio = MR::clamp(mForwardSpeed / table->mSwimToWalkSpd, 0.0f, 1.0f);

        mUpVec.dot(getPlayer()->mHeadVec);
        f32 horizontalScale = 1.25f;
        verticalScale = 1.0f;

        Vec3f jumpDir;
        MR::vecKillElement(mActor->getLastMove(), mUpVec, &jumpDir);
        MR::normalizeOrZero(&jumpDir);

        getPlayer()->mMovementStates.jumpChain = 0;
        getPlayer()->mMovementStates.forcedLaunch = 0;
        getPlayer()->mWalkSpeed = speedRatio;

        Vec3f finalVelocity = jumpDir * horizontalSpeed * horizontalScale + mUpVec * verticalBoost * verticalScale;

        getPlayer()->tryForcePowerJump(finalVelocity, false);

        mNextAction = EXIT_ACTION_POWER_JUMP;
        changeAnimationNonStop("水泳ジェット");
    }
}
