#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioRabbit.hpp"
#include "Game/Player/MarioSkate.hpp"
#include "Game/Player/MarioSukekiyo.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Player/MarioWall.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/NebulaAchievements.hpp"

namespace {
    f32 cDropFrontSpeed = 2.0f;
};  // namespace

bool Mario::isRising() const {
    if (getPlayerMode() == 4 || getPlayerMode() == 6) {
        if (mPrevVelocity.dot(*getGravityVec()) < 0.0f) {
            return true;
        }

        return false;
    }

    return mJumpVec.dot(*getGravityVec()) < 0.0f;
}

void Mario::checkWallRiseAndSlipFront() {
    mMovementStates.wallSlipUp = false;
    if (mMovementStates.frontWall) {
        mMovementStates.wallJump = false;
        mWalkSpeed = 0.0f;
        return;
    }

    mMovementStates.wallJump = true;
}

void Mario::tryJump() {
    bool isMudFloorJump = false;
    bool useStickJumpAnim = false;

    Vec3f stickTotal(mAreaMoveVelocity + mRailMoveVelocity);

    if (mMovementStates.jumping) {
        return;
    }

    if (mSinkTimer != 0) {
        if (checkCurrentFloorCodeSevere(0x19)) {
            if (mSinkTimer > 32) {
                mSinkTimer -= 32;
            }

            if (mSinkTimer < 64) {
                mSinkTimer = 64;
            }

            mMovementStates.jumpBuffered = false;
            return;
        }

        if (checkCurrentFloorCodeSevere(0x1F)) {
            return;
        }

        if (mSinkTimer > 100) {
            if (mSinkTimer > 200) {
                mSinkTimer = 128;
                changeAnimation("埋まりジャンプA");
            } else {
                mSinkTimer = 32;
                changeAnimation("埋まりジャンプB");
            }

            if (checkCurrentFloorCodeSevere(0x12)) {
                playSound("毒沼脱出");
            } else {
                playSound("砂脱出");
            }

            playSound("声砂脱出");
            return;
        }
    }

    playEffect("共通跳躍");
    stopEffectForce("スピンリング");

    const bool isSquat = checkSquat(true);
    mMovementStates.squatLocked = true;
    mMovementStates.slipUpJumped = false;
    mMovementStates.jumpBuffered = false;
    mActionStates.jumpTakeoff = true;
    mMovementStates.hipDrop = false;
    mInvincibleJump = 0;

    if (mLandTimer > mActor->getConst().getTable()->mJumpConnectTime || !mMovementStates.jumpChain) {
        mJumpKind = 0;
    } else {
        mJumpKind++;
    }

    if (mJumpKind > 2) {
        mJumpKind = 0;
    }

    if (mJumpKind == 2 && mWalkSpeed < mActor->getConst().getTable()->mJumpConnectSpeed) {
        mJumpKind = 0;
    }

    if (getPlayerMode() == 4 || getPlayerMode() == 6) {
        mJumpKind = 0;
    }

    if (mHitStates.jump2PRequest) {
        mJumpKind = 2;
        MR::start2PJumpAssistJustSound();
    } else if (mHitStates.jump2PDelayed) {
        MR::start2PJumpAssistSound();
    }

    if (isPlayerModeInvincible() && mWalkSpeed > 1.0f) {
        mJumpKind = 1;
        mInvincibleJump = 1;
    }

    if (!MR::isNearZero(stickTotal) && mSinkTimer != 0) {
        mJumpKind = 0;
        mMovementStates.turnBrake = false;
        mTurnReadyTimer = 0;
    }

    forceStopTornado();

    u32 floorCode = getFloorCode();
    if (floorCode != 0x20 && getPlayerMode() != 4 && (((mMovementStates.turnBrake) != 0) || (mTurnReadyTimer != 0 && static_cast< u8 >(checkStickFrontBack()) == 2))) {
        if ((mActor->mConst->getTable()->mTurnSlipTime - mTurnSlipTimer) >= mActor->getConst().getTable()->mTurnJumpInhibitTime) {
            tryTurnJump();
            return;
        }

        mTurnSlipTimer = 0;
        mMovementStates.turnBrake = false;
    }

    checkWallRiseAndSlipFront();

    const f32 floorAngle = calcPolygonAngleD(mGroundPolygon);
    useStickJumpAnim = false;
    if (!MR::isNearZero(mSlopeMoveVec) && (mMovementStates.slopeSlide)) {
        useStickJumpAnim = true;
    }

    if (floorAngle < 15.0f) {
        mMovementStates.slopeSlide = false;
        mSlopeMoveVec.zero();
    }

    if (getPlayerMode() != 6 && !mActionStates.steepGravityStop) {
        if (mDrawStates.onSlipFloor && isSlipPolygon(mShadowPolygon)) {
            const bool dotFront = mFrontVec.dot(mGroundNorm) < 0.0f;
            const bool dotPad = mWorldPadDir.dot(mGroundNorm) <= 0.0f;
            const bool diff = MR::diffAngleAbsHorizontal(mPrevVelocity, mGroundNorm, getAirGravityVec()) > 1.5707964f;
            const bool movementstate = mMovementStates.squat;

            if (dotFront || dotPad || diff || movementstate) {
                if (dotFront && !dotPad) {
                    mMovementStates.spinBlocked = true;
                    mJumpKind = 0;
                } else {
                    mWalkSpeed *= 0.5f;
                    if (getPlayerMode() != 4) {
                        mAirWalkTimer = 0;
                        mJumpKind = 3;
                        mMovementStates.spinBlocked = true;
                    }

                    useStickJumpAnim = false;
                }
            } else {
                mMovementStates.slopeSlide = false;
            }
        }

        if (checkCurrentFloorCodeSevere(0x11)) {
            isMudFloorJump = true;
            mJumpKind = 3;
            mWalkSpeed *= 0.5f;
        }
    }

    floorCode = getFloorCode();
    if (floorCode != 0x20 && getPlayerMode() != 4 && mJumpKind != 3) {
        if (isSquat && mWalkSpeed > 0.3f && ((mMovementStates.slopeSlide) == 0)) {
            trySquatJump();
            return;
        }

        if ((mMovementStates.squat) && ((mMovementStates.slopeSlide) == 0)) {
            tryBackJump();
            return;
        }
    }

    if (mDrawStates.homingLockOn) {
        mJumpVec = mPrevMoveFrontVec * mWalkSpeed * mActor->getConst().getTable()->mJumpFrontSpeed;
    } else {
        mJumpVec = mFrontVec * mWalkSpeed * mActor->getConst().getTable()->mJumpFrontSpeed;
    }

    mActionStates.jumpOnEnforcer = false;
    if (mDrawStates.enforcedMove) {
        recordJumpEnforceMove();

        f32 enforce;
        if (getStickP() == 0.0f) {
            mJumpVec = mRelativeMove;
            mJumpVec.zero();
            enforce = 1.0f;
        } else {
            const f32 angle = MR::diffAngleAbsHorizontal(mFrontVec, mRelativeMove, getAirGravityVec());
            enforce = 1.0f - (angle / 1.5707964f);
            if (enforce < 0.0f) {
                enforce = 0.0f;
            }

            const f32 speed = mJumpVec.length();
            const f32 speedBonus = MR::clamp(1.0f - ((speed - 4.0f) / 10.0f), 0.0f, 1.0f);
            enforce += speedBonus;
            enforce = MR::clamp(enforce, 0.0f, 1.0f);
        }

        doEnforceJump(enforce);
    }

    if (!MR::isNearZero(stickTotal)) {
        addVelocity(-stickTotal);

        MR::vecKillElement(stickTotal, mActor->mGravityVec, &stickTotal);
        cutGravityElementFromJumpVec(true);

        if (stickTotal.dot(mJumpVec) > 0.0f) {
            mJumpVec += stickTotal * 1.0f;
        } else {
            mJumpVec += stickTotal * 0.5f;
        }
    }

    if (useStickJumpAnim) {
        mJumpVec += mPrevVelocity;
        cutGravityElementFromJumpVec(true);
    }

    f32 jumpRatio = 1.0f;
    if (getFloorCode() == 0x20) {
        jumpRatio = mActor->getConst().getTable()->mMudFloorJumpWeakRatio;
    }

    if (mHitStates.jump2PRequest) {
        jumpRatio = 0.9f;
    }

    mJumpVec += -mActor->mGravityVec * mActor->getConst().getTable()->mJumpHeight[mJumpKind] * jumpRatio;

    mMovementStates.digitalJump = false;
    mMovementStates.onGround = false;
    if (getPlayerMode() != 4 && getFloorCode() == 0x20) {
        mJumpKind = 3;
        isMudFloorJump = true;
        mMovementStates.spinBlocked = true;
    }

    if (mActionStates.steepGravityStop) {
        mJumpKind = 0;
    }

    procJump(true);
    mMovementStates.jumping = true;
    // Nebula's all-time jump count: the ground jumps (one, two, three), the somersaults, the long jump, the wall kick.
    NebulaAchievements::countStat(NebulaStat_Jumps, 1);

    if (mInvincibleJump == 1 && mJumpKind == 1) {
        mJumpKind = 2;
    }

    switch (mJumpKind) {
    case 0:
        if ((mMovementStates.wallJumpQueued) != 0) {
            changeAnimation("壁上昇", "落下");
        } else {
            changeAnimation("ジャンプ", "落下");
        }

        if (mActor->mBeeWallWalk != 0) {
            playSound("ハチ壁ジャンプ");
        } else {
            playSound("小ジャンプ");
        }

        if (getPlayerMode() != 6) {
            playSound("声小ジャンプ");
            playSound("ジャンプ踏切");
        }

        if (useStickJumpAnim) {
            changeAnimation("尻滑りジャンプ");
            mAirControlInhibitTimer = 0x1E;
        }

        if (getPlayerMode() == 4) {
            changeAnimation("ハチジャンプ");
            mActor->syncJumpBeeStickMode();
        }

        if (mActionStates.steepGravityStop) {
            changeAnimation("ショートジャンプ", "落下");
        }

        break;
    case 1:
        changeAnimationNonStop("ジャンプB");
        changeAnimation(nullptr, "落下");
        playSound("中ジャンプ");
        playSound("声中ジャンプ");
        playSound("ジャンプ踏切");
        break;
    case 2:
        playSound("大ジャンプ");
        playSound("声大ジャンプ");
        playSound("ジャンプ踏切");
        playEffect("共通ハイジャンプ");

        if (mHitStates.jump2PRequest) {
            changeAnimation("スカイラブジャンプ", "落下");
        } else if (getPlayerMode() == 1 && mWalkSpeed > 1.0f) {
            changeAnimation("ダッシュジャンプ", "落下");
        } else {
            changeAnimation("ジャンプC", "落下");
        }

        break;
    case 3:
        if (isMudFloorJump) {
            changeAnimation("埋まり脱出ジャンプ", "落下");
        } else {
            changeAnimation("腹ばいジャンプ", "落下");
            playSound("声小ジャンプ");
            playSound("ジャンプ踏切");
        }

        break;
    default:
        break;
    }

    if (mInvincibleJump == 1 && mJumpKind == 2) {
        mJumpKind = 1;
    }

    const f32 waterDist = mSwim->checkUnderWaterFull(mFrontVec);
    if (waterDist > 500.0f && mStickPos.z == 0.0f) {
        mMovementStates.digitalJump = true;
        changeAnimationNonStop("飛び込みジャンプ");
        playSound("声高飛び込み");
        playSound("ジャンプ踏切");

        if (mStickPos.z < 0.1f) {
            mJumpVec += mFrontVec * 5.0f;
        }
    }

    mMovementStates.jumpChain = true;
    return;
}

void Mario::tryTurnJump() {
    mJumpKind = 0x4;

    if (mTurnReadyTimer != 0) {
        Vec3f horizontal;
        MR::vecKillElement(mTurnSlipFrontVec, getAirGravityVec(), &horizontal);
        MR::normalizeOrZero(&horizontal);

        const f32 turnJumpFrontSpeed = mActor->getConst().getTable()->mTurnJumpFrontSpeed;
        mJumpVec = -horizontal * turnJumpFrontSpeed;

        setFrontVecKeepUp(-mTurnSlipFrontVec);
        mTurnFrontVec = mTurnSlipFrontVec;
    } else {
        Vec3f horizontal;
        MR::vecKillElement(mTurnFrontVec, getAirGravityVec(), &horizontal);
        MR::normalizeOrZero(&horizontal);

        const f32 turnJumpFrontSpeed = mActor->getConst().getTable()->mTurnJumpFrontSpeed;
        mJumpVec = -horizontal * turnJumpFrontSpeed;
    }

    const f32 jumpHeight = mActor->getConst().getTable()->mJumpHeight[mJumpKind];
    mJumpVec += -mActor->mGravityVec * jumpHeight;

    mActionStates.jumpOnEnforcer = false;
    if (mDrawStates.enforcedMove) {
        recordJumpEnforceMove();
        doEnforceJump(1.0f);
    }

    mMovementStates.digitalJump = true;
    procJump(true);
    changeAnimation("ターンジャンプ", "落下");
    playSound("後ジャンプ");
    playSound("声後ジャンプ");
    playSound("ジャンプ踏切");
    playEffect("共通ハイジャンプ");

    mTurnSlipTimer = 0;
    mMovementStates.jumping = true;
    mMovementStates.onGround = false;
    mMovementStates.turnBrake = false;
    mMovementStates.jumpChain = false;
    mWalkSpeed = 0.0f;
    mActionStates.retainInAir = false;
    NebulaAchievements::countStat(NebulaStat_Jumps, 1);
    return;
}

void Mario::trySquatJump() {
    mMovementStates.squatLocked = false;
    mJumpKind = 0x5;

    mJumpVec = getAirFrontVec() * mActor->getConst().getTable()->mSquatJumpFrontSpeed;

    Vec3f stickVec(mAreaMoveVelocity + mRailMoveVelocity);
    if (!MR::isNearZero(stickVec)) {
        addVelocity(-stickVec);

        MR::vecKillElement(stickVec, mActor->mGravityVec, &stickVec);
        cutGravityElementFromJumpVec(true);

        mJumpVec += stickVec * 1.0f;
    }

    const f32 jumpHeight = mActor->getConst().getTable()->mJumpHeight[mJumpKind];
    mJumpVec += -mActor->mGravityVec * jumpHeight;

    mActionStates.jumpOnEnforcer = false;
    if (mDrawStates.enforcedMove) {
        recordJumpEnforceMove();

        Vec3f moveKeep;
        f32 gravityMove = MR::vecKillElement(mRelativeMove, getAirGravityVec(), &moveKeep);

        f32 enforceScale = 1.0f - (MR::diffAngleAbs(mFrontVec, moveKeep) / 1.5707964f);
        if (enforceScale < 0.0f) {
            enforceScale = 0.0f;
        }

        const f32 jumpMag = mJumpVec.length();
        const f32 jumpScale = MR::clamp(1.0f - ((jumpMag - 4.0f) / 10.0f), 0.0f, 1.0f);
        enforceScale += jumpScale;
        enforceScale = MR::clamp(enforceScale, 0.0f, 1.0f);

        if (mJumpVec.dot(moveKeep) < 0.0f) {
            mDrawStates.keepJumpParam = true;
            initActiveJumpVec();
        }

        mVelocity -= moveKeep;

        mJumpVec += moveKeep * enforceScale;

        if (gravityMove < 0.0f) {
            mJumpVec += getAirGravityVec() * gravityMove;
        }

        invalidateRelativePosition();
    }

    mMovementStates.digitalJump = true;
    mMovementStates.onGround = false;
    procJump(true);
    changeAnimationNonStop("幅とび");
    changeAnimation(nullptr, "落下");
    playSound("幅ジャンプ");
    playSound("声幅ジャンプ");
    playSound("ジャンプ踏切");

    mMovementStates.jumping = true;
    mTurnSlipTimer = 0;
    mMovementStates.jumpChain = false;
    mMovementStates.turnBrake = false;
    NebulaAchievements::countStat(NebulaStat_Jumps, 1);
    return;
}

void Mario::tryBackJump() {
    mMovementStates.squatLocked = true;
    mMovementStates.squat = false;
    mJumpKind = 0x6;

    const f32 backSpeed = mActor->getConst().getTable()->mSquatJumpBackSpeed;
    mJumpVec = -getAirFrontVec() * backSpeed;

    const f32 jumpHeight = mActor->getConst().getTable()->mJumpHeight[mJumpKind];
    mJumpVec += -mActor->mGravityVec * jumpHeight;

    mActionStates.jumpOnEnforcer = false;
    if (mDrawStates.enforcedMove) {
        recordJumpEnforceMove();
        doEnforceJump(1.0f);
    }

    mMovementStates.digitalJump = true;
    procJump(true);
    changeAnimation("しゃがみジャンプ", "落下");
    playSound("後ジャンプ");
    playSound("声後ジャンプ");
    playSound("ジャンプ踏切");
    playEffect("共通ハイジャンプ");

    if (mSwim->checkUnderWaterFull(-mFrontVec) > 500.0f && mStickPos.z == 0.0f) {
        changeAnimationNonStop("後方飛び込みジャンプ");
        playSound("声高飛び込み");
        playSound("ジャンプ踏切");
    }

    mWalkSpeed = 0.0f;
    mMovementStates.jumping = true;
    NebulaAchievements::countStat(NebulaStat_Jumps, 1);
    mTurnSlipTimer = 0;
    mMovementStates.onGround = false;
    mMovementStates.turnBrake = false;
    mMovementStates.jumpChain = false;
    return;
}

void Mario::tryTornadoJump() {
    mJumpKind = 0x8;
    mInvincibleJump = 0;

    const f32 frontSpeed = mWalkSpeed;
    const f32 tornadoSpeed = mActor->getConst().getTable()->mJumpTornadoSpeed;

    mJumpVec = mFrontVec * frontSpeed * tornadoSpeed;

    const f32 jumpHeight = mActor->getConst().getTable()->mJumpHeight[mJumpKind];
    mJumpVec += -mActor->mGravityVec * jumpHeight;

    mMovementStates.digitalJump = false;
    mMovementStates.tornado = true;
    procJump(true);
    changeAnimation("ジャンプ", "落下");
    playSound("声スピン");
    playSound("トルネードジャンプ");

    mMovementStates.jumping = true;
    mTurnSlipTimer = 0;
    mMovementStates.turnBrake = false;
    mMovementStates.jumpChain = false;
    mMovementStates.waitStickNeutral = false;
    mWalkSpeed = 0.0f;

    mAirWalkTimer = mActor->getConst().getTable()->mAirWalkTimeTornado;
    mZeroGravityTimer = mActor->getConst().getTable()->mTornadoZeroGravityTimer;

    const f32 boosterPower = mActor->getConst().getTable()->mTornadoBoostPower;
    setRocketBooster(-(*getGravityVec()) * boosterPower, mActor->getConst().getTable()->mTornadoBoostAttn,
                     mActor->getConst().getTable()->mTornadoBoostTimer);
}

void Mario::startTornadoCentering(HitSensor* pSensor) {
    pushTask(reinterpret_cast< Task >(&Mario::taskOnTornadoCentering), 0x400);
    mTornadoCenteringSensor = pSensor;
    mTornadoCenteringTimer = 0x1E;
}

bool Mario::taskOnTornadoCentering(u32) {
    if (mTornadoCenteringTimer != 0) {
        Vec3f sensorOffset(mTornadoCenteringSensor->mHost->mPosition - mPosition);
        MR::vecKillElement(sensorOffset, *getGravityVec(), &sensorOffset);

        f32 mag = sensorOffset.length();
        if (mag < 10.0f) {
            mag = 10.0f;
        }

        f32 ratio = 10.0f / mag;
        if (ratio < 0.1f) {
            ratio = 0.1f;
        }

        addTrans(sensorOffset * ratio);

        mTornadoCenteringTimer--;

        if (mTornadoCenteringTimer == 0) {
            return false;
        }

        return true;
    }

    return false;
}

void Mario::trySpinJump(u8 subSpin) {
    if (mMovementStates.hipDrop) {
        return;
    }

    if (mJumpKind == 8) {
        return;
    }

    if (isStatusActive(0x17)) {
        return;
    }

    if (getPlayerMode() == 4) {
        return;
    }

    if (getPlayerMode() == 6) {
        return;
    }

    if (getPlayerMode() == 7 && mVerticalSpeed > 100.0f) {
        tryStartFoo();
        return;
    }

    if (isAnimationRun("水泳スピン移動")) {
        return;
    }

    if (isAnimationRun("水泳スピンジャンプ")) {
        return;
    }

    mJumpKind = 0x8;
    mInvincibleJump = subSpin + 1;

    const f32 frontSpeed = mWalkSpeed;
    const f32 tornadoSpeed = mActor->getConst().getTable()->mJumpTornadoSpeed;
    mJumpVec = mFrontVec * frontSpeed * tornadoSpeed;

    const f32 spinJumpHeight = mActor->getConst().getTable()->mSpinJumpHeight;
    mJumpVec += -mActor->mGravityVec * spinJumpHeight;

    mMovementStates.digitalJump = false;
    mAirWalkTimer = mActor->getConst().getTable()->mAirWalkTimeSpin;
    procJump(true);

    if (!mMovementStates.hipDrop) {
        stopAnimationUpper(nullptr);
        changeAnimation("空中ひねり", "落下");
        playSound("声スピン");
        playSound("スピンジャンプ");
        startPadVib(2);

        mMovementStates.jumping = true;
        mMovementStates.turnBrake = false;
        mMovementStates.jumpChain = false;
        mMovementStates.waitStickNeutral = false;
        mActionStates.spinJumpLand = true;
        mTurnSlipTimer = 0;
        mWalkSpeed = 0.0f;
        mActionStates.swingEffectPending = true;

        if (isStatusActive(MarioStatus_Rabbit)) {
            mRabbit->hop();
        }
    }
}

void Mario::tryForceJumpDelay(const Vec3f& rVec) {
    mMovementStates.forceJumpDelay = true;
    mForceJumpVec = rVec;
}

void Mario::tryFreeJumpDelay(const Vec3f& rVec) {
    mMovementStates.freeJumpDelay = true;
    mForceJumpVec = rVec;
}

void Mario::tryForceJump(const Vec3f& rVec, bool isDigital) {
    if (rVec.dot(*getGravityVec()) > 0.0f) {
        mMovementStates.noDropSpeedCap = true;
    }

    mMovementStates.jumping = true;
    mMovementStates.onGround = false;
    mMovementStates.jumpChain = false;
    mMovementStates.digitalJump = isDigital;
    mMovementStates.hipDrop = false;
    mJumpVec = rVec;

    if (getPlayerMode() == 4 || getPlayerMode() == 6) {
        mPrevVelocity = mJumpVec;
    }

    resetTornado();

    mAirTimer = 0;
    mBackJumpTimer = 0;
    mBackAirWalkTimer = 0;
    mMovementStates.jumpHeld = true;
    mMovementStates.beeFly = false;
    mActionStates.rollLanding = false;
    mJumpTimer = 0;
    mFixSideVec = mSideVec;
    mJumpStartPos = mPosition;
    mFireShotCount = 0;
    mBeeGravityReviveTimer = 0;
    procJump(false);
    changeAnimation(nullptr, "落下");
    mRabbit->forceJump();
    return;
}

void Mario::tryForceFreeJump(const Vec3f& rVec) {
    mMovementStates.wallSlipUp = false;
    mJumpKind = 0xC;
    mMovementStates.slipUpBlocked = true;
    mMovementStates.wallJump = false;

    tryForceJump(rVec, true);
    mAirTimer = 0;
    mMovementStates.beeFly = false;
    mAirWalkTimer = mActor->getConst().getTable()->mAirWalkTime;

    if (getPlayerMode() == 4) {
        playSound("ハチ体力完全回復");
    }

    mBeeGravityReviveTimer = 0;
    return;
}

void Mario::tryForcePowerJump(const Vec3f& rVec, bool keepAirWalk) {
    mMovementStates.wallSlipUp = false;
    mJumpKind = 0xD;
    mMovementStates.slipUpBlocked = true;
    mMovementStates.wallJump = false;
    const bool forcedLaunchPrev = mMovementStates.forcedLaunch;
    mMovementStates.forcedLaunch = true;
    tryForceJump(rVec, true);
    mMovementStates.forcedLaunch = forcedLaunchPrev;
    mAirTimer = 0;
    mMovementStates.beeFly = false;

    if (!keepAirWalk) {
        mAirWalkTimer = mActor->getConst().getTable()->mAirWalkTime;

        if (getPlayerMode() == 4) {
            playSound("ハチ体力完全回復");
        }
    }

    mBeeGravityReviveTimer = 0;
    return;
}

void Mario::tryFreeJump(const Vec3f& rVec, bool isDigital) {
    mJumpKind = 0;

    if (rVec.dot(*getGravityVec()) > 0.0f) {
        mMovementStates.noDropSpeedCap = true;
    }

    mMovementStates.jumping = true;
    mMovementStates.onGround = false;
    mMovementStates.jumpChain = false;
    mMovementStates.digitalJump = isDigital;
    mMovementStates.hipDrop = false;
    mJumpVec = rVec;
    resetTornado();

    mAirTimer = 0;
    mBackJumpTimer = 0;
    mBackAirWalkTimer = 0;
    mMovementStates.jumpHeld = true;
    mMovementStates.beeFly = false;
    mJumpTimer = 0;
    mBeeGravityReviveTimer = 0;
    mFixSideVec = mSideVec;
    procJump(false);
    changeAnimation(nullptr, "落下");
    startPadVib(2);
    mRabbit->forceJump();
    return;
}

void Mario::tryWallJump(const Vec3f& rVec, bool isWallKick) {
    mJumpKind = 0x7;

    mMovementStates.jumping = true;
    NebulaAchievements::countStat(NebulaStat_Jumps, 1);
    mMovementStates.jumpChain = false;
    mMovementStates.digitalJump = true;
    mMovementStates.hipDrop = false;
    mMovementStates.wallSlipUp = false;
    mMovementStates.slipUpJumped = false;

    if (isWallKick) {
        mMovementStates.wallJump = true;
    } else {
        mMovementStates.wallJump = false;
    }

    mJumpVec = rVec;
    mJumpStartPos = mPosition;
    mAirTimer = 0;
    mMovementStates.jumpHeld = true;
    mMovementStates.beeFly = false;
    mBackJumpTimer = 0;
    mBackAirWalkTimer = 0;
    mJumpTimer = 0;
    mMovementStates.waitStickNeutral = true;

    if (getPlayerMode() == 5) {
        changeAnimationNonStop("ホッパー壁ジャンプ");
        playSound("ホッパージャンプ");
    } else {
        changeAnimation("壁ジャンプ", "落下");
        playSound("ジャンプ踏切");
    }

    playSound("声小ジャンプ");

    if (isWallKick) {
        setFrontVecKeepUp(rVec);
    }

    stopWalk();
    mRabbit->forceJump();
    return;
}

void Mario::tryStickJump(const Vec3f& rVec) {
    stopJump();
    initJumpParam();

    mMovementStates.jumping = true;
    mMovementStates.jumpChain = false;
    mMovementStates.digitalJump = true;
    mMovementStates.hipDrop = false;
    mJumpKind = 0;
    mMovementStates.slipUpJumped = false;
    mMovementStates.wallSlipUp = false;
    mMovementStates.wallJump = true;
    mJumpVec = rVec;
    mBackJumpTimer = 0;
    mBackAirWalkTimer = 0;
    mJumpTimer = 0;
    mMovementStates.jumpHeld = true;
    mMovementStates.beeFly = false;
    mMovementStates.waitStickNeutral = true;
    mBeeGravityReviveTimer = mActor->getConst().getTable()->mBeeGravityReviveTime;
    mBeeUpDownSpeed = 0.0f;
    mAirTimer = 0xA;
    playSound("ハチ壁ジャンプ");
    playSound("声小ジャンプ");
    setFrontVecKeepUp(rVec);
    changeAnimationNonStop("ハチ壁ジャンプ");
    return;
}

void Mario::trySlipUpJump() {
    if (mMovementStates.onGround) {
        mMovementStates.wallSlipUp = false;
        return;
    }

    changeAnimation("スリップアップ");
    playSound("スリップアップ");
    playEffect("スリップアップ");
    startPadVib("マリオ[スリップアップ]");

    mJumpVec = -getAirGravityVec() * mActor->getConst().getTable()->mSlipUpHeight;

    f32 ratio = 1.0f;
    Vec3f predictPos(mPosition - *getGravityVec() * 100.0f);

    Vec3f frontVec;
    MR::vecKillElement(mJumpVec, mFrontVec, &frontVec);
    MR::vecKillElement(frontVec, *getGravityVec(), &frontVec);
    frontVec += mFrontVec * mActor->getConst().getTable()->mSlipUpFront;
    MR::normalizeOrZero(&frontVec);

    f32 step = 50.0f;
    u32 hitCount;
    Vec3f hitPos;
    for (hitCount = 0; hitCount < 10; hitCount++) {
        Triangle* pTmpPolygon = getTmpPolygon();
        predictPos += frontVec * step;

        if (!MR::getFirstPolyOnLineBFast(predictPos, *getGravityVec() * 150.0f, &hitPos, pTmpPolygon)) {
            break;
        }
    }

    ratio *= static_cast< f32 >(hitCount) / 10.0f;
    cutVecElementFromJumpVec(mFrontVec);

    if (mMovementStates.wallJump) {
        mJumpVec += mFrontVec * mActor->getConst().getTable()->mSlipUpFront * ratio;
        mMovementStates.slipUpJumped = true;
    } else {
        mJumpVec += mFrontVec * mActor->getConst().getTable()->mSlipUpFrontWeak * ratio;
    }

    mMovementStates.wallSlipUp = false;
    mMovementStates.digitalJump = true;
    initJumpParam();
    mJumpKind = 0xA;
    return;
}

void Mario::tryHangSlipUp() {
    mMovementStates.jumpChain = false;
    getPlayer()->tryJump();
    changeAnimation("つかまりスリップアップ");
    playSound("スリップアップ");
    playEffect("スリップアップ");
    startPadVib("マリオ[スリップアップ]");

    const f32 slipUpHeight = mActor->getConst().getTable()->mSlipUpHeightHang;
    mJumpVec = -getAirGravityVec() * slipUpHeight;
    cutVecElementFromJumpVec(mFrontVec);

    const f32 slipUpFront = mActor->getConst().getTable()->mSlipUpFrontHang;
    mJumpVec += mFrontVec * slipUpFront;

    mMovementStates.wallSlipUp = false;
    mMovementStates.digitalJump = true;
    mJumpKind = 0xA;
    mActor->setBlendMtxTimer(0x8);
    mTargetWalkSpeedIndex = 0x7;
    mWalkSpeed = 0.6f;
    return;
}

void Mario::tryDrop() {
    if ((mMovementStates.frontWall) && !mDrawStates.enforcedMove) {
        Vec3f horizontal;
        MR::vecKillElement(mFrontWallPos - mGroundPos, *getGravityVec(), &horizontal);

        if (horizontal.dot(*mFrontWallTriangle->getNormal(0)) < 0.0f && horizontal.length() < 80.0f) {
            getPlayer()->mMovementStates.onGround = true;
            push(*mFrontWallTriangle->getNormal(0) * (80.0f - horizontal.length()) * 0.1f);
            return;
        }
    }

    if ((mMovementStates.sideWall) && !mDrawStates.enforcedMove) {
        Vec3f horizontal;
        MR::vecKillElement(mSideWallPos - mGroundPos, *getGravityVec(), &horizontal);

        if (horizontal.dot(*mSideWallTriangle->getNormal(0)) < 0.0f && horizontal.length() < 80.0f) {
            push(*mSideWallTriangle->getNormal(0) * (80.0f - horizontal.length()) * 0.1f);
            getPlayer()->mMovementStates.onGround = true;
            return;
        }
    }

    mMovementStates.squatLocked = true;
    mMovementStates.hipDrop = false;

    mJumpVec = mFrontVec * mWalkSpeed * ::cDropFrontSpeed;

    if (MR::isNearZero(mSlopeMoveVec) && (mMovementStates.slopeSlide)) {
        Vec3f side;
        side.cross(getAirGravityVec(), mGroundNorm);
        MR::normalizeOrZero(&side);
        mSlopeMoveVec.cross(mGroundNorm, side);
        MR::normalizeOrZero(&mSlopeMoveVec);
    }

    if (!MR::isNearZero(mSlopeMoveVec)) {
        mJumpVec = mSlopeMoveVec;
        cutGravityElementFromJumpVec(false);

        if (mPrevVelocity.length() > 20.0f) {
            mJumpVec += -mActor->getAirGravityVec() * 10.0f;
        } else {
            f32 gravScale = mActor->mLastPushMove.length();
            if (gravScale > 8.0f) {
                gravScale = 8.0f;
            }

            mJumpVec += -mActor->getAirGravityVec() * gravScale;
            mSlopeMoveVec.zero();
        }
    }

    Vec3f carryVec(mRelativeMove);
    MR::vecKillElement(carryVec, *getGravityVec(), &carryVec);
    invalidateRelativePosition();

    Vec3f groundDeltaNoGravity;
    MR::vecKillElement(mGroundPos - mPosition, *getGravityVec(), &groundDeltaNoGravity);

    if (carryVec.dot(groundDeltaNoGravity) > 0.0f) {
        carryVec = carryVec * 0.1f;
    }

    mJumpVec += carryVec;

    mJumpKind = 0;
    procJump(true);

    mMovementStates.jumping = true;
    mMovementStates.jumpChain = false;
    mMovementStates.digitalJump = true;
    mActionStates.jumpTakeoff = false;
    mMovementStates.hipDrop = false;

    if (isAnimationRun("坂すべり上向きうつぶせ", MarioIndexedAnime_SlideUpFaceDown) || isAnimationRun("坂すべり下向きあおむけ", MarioIndexedAnime_SlideDownFaceUp)) {
        mAirControlInhibitTimer = 0xF;
    }

    changeAnimation(nullptr, "落下");

    if (getPlayerMode() == 4) {
        mBeeGravityTimer = mActor->getConst().getTable()->mBeeGravityPowerTimeD;
        mAirTimer = mActor->getConst().getTable()->mBeeAirWalkInhibitTimeD - 5;
    }

    if (isAnimationRun("ショート着地")) {
        mMovementStates.slopeSlide = true;
    }

    if (mMovementStates.frontWall) {
        stopWalk();
    }

    return;
}

bool Mario::isDigitalJump() const {
    return mMovementStates.digitalJump;
}

void Mario::initActiveJumpVec() {
    mJumpGravityVec = getAirGravityVec();
    MR::vecKillElement(mJumpVec, mJumpGravityVec, &mJumpFrontVec);

    if (MR::normalizeOrZero(&mJumpFrontVec)) {
        mJumpFrontVec = mFrontVec;
    }
}

void Mario::initJumpParam() {
    mMovementStates.jumpHeld = true;

    if ((mHitStates.jump2PDelayed) || (mHitStates.jump2PRequest)) {
        mActionStates.jump2P = true;
    }

    mMovementStates.jumpBuffered = false;
    mActionStates.rollLanding = false;
    mMovementStates.forcedLaunch = false;
    mActionStates.wallJumpHit = false;

    if (!mDrawStates.keepJumpParam) {
        initActiveJumpVec();
    }

    mFireShotCount = 0;
    mAirTimer = 0;
    mBackJumpTimer = 0;
    mBackAirWalkTimer = 0;
    mBeeFlyInhibitTimer = 0;
    mMovementStates.beeFly = false;
    mJumpTimer = 0;
    mFixSideVec = mSideVec;

    mBeeGravityTimer = 0;
    mBeeGravityReviveTimer = 0;
    mMovementStates.spinStartWithSquat = false;
    mMovementStates.turnStickNeutral = false;
    mMovementStates.turnBrake = false;
    mMovementStates.noDropSpeedCap = false;
    mActionStates.uTurn = false;
    mTurnReadyTimer = 0;
    mTurnSlipTimer = 0;
    mLandTimer = 0;
    mMovementStates.slipUpBlocked = mMovementStates.frontWall;

    if (mJumpKind != 5) {
        cancelSquatMode();
    }

    mJumpStartPos = mPosition;
    mTornadoBoostTimer = 0;

    if (mSinkTimer != 0) {
        mJumpVec.scale(0.2f + (0.8f * ((0x100 - mSinkTimer) / 256.0f)));
        mSinkTimer = 0;
    }
}

bool Mario::isEnableFutureJump() const {
    if (mJumpKind == 0x3) {
        return false;
    }

    if (!isRising()) {
        const f32 dot = mJumpVec.dot(*getGravityVec());
        const s16 limit = mActor->getConst().getTable()->mFutureJumpReqLimitTime;
        if (mVerticalSpeed < dot * static_cast< f32 >(limit)) {
            return true;
        }
    }

    return false;
}

void Mario::procJump(bool atStart) {
    f32 jumpGravity;
    f32 gravityScale = 1.0f;
    if (!atStart && !mActionStates.jump2P && mJumpKind == 0 && mAirTimer < 6 && mActor->isRequestJump2P()) {
        mHitStates.jump2PRequest = true;
        mMovementStates.jumping = false;
        mAirTimer = 6;
        mActionStates.jump2P = true;
        tryJump();
        mAirTimer = 6;
    }

    if (mMovementStates.hipDrop) {
        procHipDrop();
        return;
    }

    if (mZeroGravityTimer != 0) {
        mZeroGravityTimer--;
        return;
    }

    procRocketBooster();

    if (mActionStates.inTower) {
        fixFrontVecByGravity();
    }

    if (mActionStates.jump2P) {
        if (!mActor->isKeepJump2P()) {
            mMovementStates.jumpHeld = false;
        }
    } else if (!mActor->isKeepJump()) {
        mMovementStates.jumpHeld = false;
    }

    if (checkTrgA()) {
        mJumpFrame = mActor->mFrameCount;
        if (isEnableFutureJump()) {
            mMovementStates.jumpBuffered = true;
        }
    }

    if (mActor->isRequestJump2P()) {
        mJumpFrame = mActor->mFrameCount;
        if (isEnableFutureJump()) {
            mMovementStates.jumpBuffered = true;
        }
    }

    bool useConnectGravity = false;
    if (mMovementStates.jumpHeld && !isDigitalJump() && mJumpKind == 9 && isRising()) {
        useConnectGravity = true;
    }

    if (atStart || mMovementStates.jumpHeld || isDigitalJump() || !isRising() || mJumpKind == 2) {
        useConnectGravity = true;
    }

    if (mJumpKind == 8 && mInvincibleJump == 0) {
        s32 timer = mTornadoBoostTimer;
        if (timer == 0) {
            useConnectGravity = true;
        } else {
            useConnectGravity = timer > (mActor->getConst().getTable()->mTornadoBoostTimer - 0x10);
        }
    }

    if (useConnectGravity) {
        MarioConstTable* pTable = mActor->getConst().getTable();
        gravityScale = pTable->mGravityRatioA;
    }

    if (atStart) {
        initJumpParam();
    } else if (mActor->isRequestHipDrop() && jumpToHipDrop()) {
        return;
    }

    if (!isRising()) {
        if (isAnimationRun("ジャンプB")) {
            stopAnimation(nullptr);
        }

        if (!mSwim->mDeepWater) {
            if (isAnimationRun("水泳スピン移動")) {
                stopAnimation(nullptr);
            }

            if (isAnimationRun("水泳スピンジャンプ")) {
                stopAnimation(nullptr);
            }

            if (isAnimationRun("水泳ジェット")) {
                changeAnimation("空中一回転");
            }
        }

        mJumpTimer++;
    } else {
        f32 ceilDist = calcDistToCeil(false);
        if (ceilDist < 160.0f) {
            jumpGravity = cutGravityElementFromJumpVec(true);

            playEffectTrans("天井ヒット", mPosition - getAirGravityVec() * ceilDist);

            f32 reduce;
            switch (mJumpKind) {
            case 4:
            case 6:
                reduce = 0.9f;
                break;
            case 5:
            default:
                reduce = 0.6f;
                break;
            }

            mJumpVec += getAirGravityVec() * jumpGravity * reduce;
        }
    }

    bool wasAirWalk = false;
    if (mBackAirWalkTimer != 0) {
        wasAirWalk = true;
        mBackAirWalkTimer--;
    }

    if (mAirTimer > 10 && checkTrgA() && (mMovementStates.wallJumpQueued)) {
        mMovementStates.wallJumpQueued = false;
        if (mWall->startJump()) {
            return;
        }
    }

    if (mJumpKind == 8) {
        if (checkTrgZ()) {
            resetTornado();
            cancelTornadoJump();
            stopAnimation(nullptr);
            changeAnimationInterpoleFrame(1);
            mDrawStates.endTornado = true;
            playSound("声小ジャンプ");

            if (mInvincibleJump != 0) {
                playEffect("スピンライト消去");
            }
        } else {
            mMovementStates.beeFly = false;
        }

        if (mInvincibleJump != 0) {
            if (mAirWalkTimer != 0) {
                mAirWalkTimer--;
            }

            if (mAirWalkTimer == 0) {
                cancelTornadoJump();
                playEffect("スピンライト消去");

                if (getPlayerMode() != 4) {
                    mAirWalkTimer = mActor->getConst().getTable()->mAirWalkTime;
                }
            }
        }
    }

    if (beeMarioOnAir()) {
        mBeeGravityReviveTimer = mActor->getConst().getTable()->mBeeGravityReviveTime;
    }

    f32 jumpAcceleration;
    if (mJumpKind == 0xC) {
        jumpAcceleration = 1.8f;
    } else if (mJumpKind == 0xD) {
        jumpAcceleration = mActor->getConst().getTable()->mGravityJumping[9];
    } else {
        jumpAcceleration = mActor->getConst().getTable()->mGravityJumping[mJumpKind];
    }

    if (mJumpKind == 0xB && (isRising() || mJumpTimer < mActor->getConst().getTable()->mTrampleBegomaOpenTime)) {
        jumpAcceleration = mActor->getConst().getTable()->mGravityJumping[0];
    }

    if (mJumpKind == 8 && mInvincibleJump == 1) {
        jumpAcceleration = mActor->getConst().getTable()->mSpinJumpGravity;
    }

    if (mJumpKind == 8 && !isRising()) {
        jumpAcceleration *= 0.25f;
    }

    const s32 clingNum = MR::getKarikariClingNum();
    f32 clingScale = 1.0f;
    if (clingNum != 0) {
        clingScale = (2.0f + static_cast< f32 >(clingNum)) / 2.0f;
        if (getPlayerMode() == 4) {
            clingScale = 1.0f;
        }

        jumpAcceleration *= clingScale;
    }

    if (wasAirWalk) {
        jumpAcceleration = mActor->getConst().getTable()->mGravityAirWalk;
    }

    if (mBeeGravityReviveTimer != 0) {
        const u16 maxTimer = mActor->mConst->getTable()->mBeeGravityReviveTime;
        const f32 t = static_cast< f32 >(mBeeGravityReviveTimer) / static_cast< f32 >(maxTimer);
        jumpAcceleration = ((1.0f - t) + (t * mActor->getConst().getTable()->mGravityAirWalk));
        mBeeGravityReviveTimer--;
        jumpAcceleration *= clingScale;
    }

    if (mJumpKind == 0xD && (isDigitalJump() || !isRising())) {
        gravityScale = 1.0f;
    }

    gravityScale *= mActor->getGravityRatio() * NebulaMods::gravityScale();
    if (mActor->mGravityJumpTimer != 0) {
        gravityScale *= 0.3f;
        if (!mActor->isInZeroGravitySpot() && mVerticalSpeed < 300.0f) {
            gravityScale = 0.0f;
            if (mVerticalSpeed < 200.0f) {
                cutGravityElementFromJumpVec(true);
                gravityScale = -0.1f;
            }
        }
    }

    if (getPlayerMode() == 6) {
        gravityScale = 0.0f;
    }

    if (mMovementStates.wallJumpQueued) {
        f32 wallRatio = static_cast< f32 >(mAirTimer) / static_cast< f32 >(mActor->mConst->getTable()->mSlipUpSpdCtrlTimer);
        if (wallRatio > 1.0f) {
            wallRatio = 1.0f;
        }

        const f32 wallScale =
            mActor->getConst().getTable()->mSlipUpSpdRatio + ((1.0f - wallRatio) * (1.0f - mActor->getConst().getTable()->mSlipUpSpdRatio));
        addVelocity(mJumpVec, wallScale);

        mJumpVec += mActor->mGravityVec * jumpAcceleration * gravityScale * wallScale;
        moveWallSlide(1.0f);
    } else if ((mMovementStates.onGround) == 0) {
        addVelocity(mJumpVec);

        mJumpVec += mActor->mGravityVec * jumpAcceleration * gravityScale;
    }

    checkWallRising();
    checkWallJumpHit();

    if ((mMovementStates.onGround) == 0) {
        fixFrontVecFromUpSide();
    }

    decideSlipUp();

    if (mJumpKind != 0xD || !mMovementStates.noDropSpeedCap) {
        Vec3f side;
        const f32 dropSpeed = MR::vecKillElement(mJumpVec, *getGravityVec(), &side);

        f32 maxDrop = mActor->getConst().getTable()->mMaxDropSpeed[mActor->getGravityLevel() & 0xFF];
        if (mJumpKind == 0xB && !isRising() && mJumpTimer >= mActor->mConst->getTable()->mTrampleBegomaOpenTime) {
            maxDrop = mActor->getConst().getTable()->mMaxDropSpeed[3];
        }

        if (getPlayerMode() == 4 && !isRising()) {
            maxDrop = mActor->getConst().getTable()->mMaxDropSpeed[4];
        }

        if (dropSpeed >= maxDrop) {
            mJumpVec = *getGravityVec() * maxDrop + side;
        }
    }

    if ((mMovementStates.onGround) != 0 && !atStart) {
        doLanding();

        if (calcDistToCeil(false) < 80.0f) {
            bool pressedA = false;
            const HitSensor* sensorA = mCeilSensor;
            if (sensorA != nullptr && MR::isSensorPressObj(sensorA)) {
                pressedA = true;
            }

            bool pressedB = false;
            const HitSensor* sensorB = mGroundPolygon->mSensor;
            if (sensorB != nullptr && MR::isSensorPressObj(sensorB)) {
                pressedB = true;
            }

            if (mCeilSensor != mGroundPolygon->mSensor && (pressedA || pressedB)) {
                mActor->setPress(0, 0);
                mActor->mPressAxis = *mGroundPolygon->getNormal(0);
            }
        }
    } else {
        doAirWalk();
    }

    mAirTimer++;
    return;
}

void Mario::checkWallRising() {
}

void Mario::checkWallJumpHit() {
    if (mJumpKind == 0x5) {
        if ((mMovementStates.frontWall) && !checkWallJumpCode()) {
            const f32 wallDot = cutVecElementFromJumpVec(getWallNorm());
            if (checkWallCode(CollisionWallCode_NoAction, true)) {
                return;
            }

            mJumpVec -= getWallNorm() * wallDot;

            blown(mJumpVec * 0.2f);

            Vec3f horizontal;
            MR::vecKillElement(mJumpVec, *getGravityVec(), &horizontal);
            MR::normalizeOrZero(&horizontal);
            if (!MR::isNearZero(horizontal)) {
                setFrontVecKeepUp(-horizontal);
            }

            mMovementStates.spinBlocked = true;
            mAirWalkTimer = 0;
            mAirControlInhibitTimer = 0x3C;
        }

        return;
    }

    if (!isRising() && mAirTimer > 10 && (mMovementStates.frontWall) && calcPolygonAngleD(mFrontWallTriangle) < 80.0f) {
        if ((mActionStates.wallJumpHit) == 0) {
            mJumpVec = *mFrontWallTriangle->getNormal(0) * 5.0f + getAirGravityVec() * cutGravityElementFromJumpVec(true);
            mActionStates.wallJumpHit = true;
            changeAnimation("壁はじき");
        } else {
            const f32 jumpMag = mJumpVec.length();
            cutVecElementFromJumpVec(*mFrontWallTriangle->getNormal(0));
            mJumpVec.setLength(jumpMag);
        }
    }
}

void Mario::decideSlipUp() {
    if (isAnimationRun("壁はじき")) {
        return;
    }

    if (!mMovementStates.wallJump) {
        return;
    }

    if (mMovementStates.tornado) {
        mMovementStates.wallSlipUp = false;
        return;
    }

    if (!mMovementStates.wallSlipUp) {
        if (checkWallCode(CollisionWallCode_NotWallSlip, true)) {
            return;
        }

        if (checkWallCode(CollisionWallCode_NoAction, true)) {
            return;
        }

        bool slipFront = false;
        if (isStickOn()) {
            const Vec3f& worldPadDir = getWorldPadDir();
            if (worldPadDir.dot(getWallNorm()) < -0.8f) {
                slipFront = true;
            }
        }

        if (mJumpKind == 0x7) {
            slipFront = true;
        }

        if ((mMovementStates.slipUpBlocked) == 0 && (mMovementStates.frontWall) && isRising() && slipFront) {
            mMovementStates.wallSlipUp = true;
        }

        if (isRising() && getPlayerMode() != 4 && getPlayerMode() != 6 && getPlayerMode() != 5) {
            if (mFrontFloorHeight < mActor->getConst().getTable()->mSlipUpContinueHeight) {
                if (mMovementStates.frontWall && mMovementStates.wallJump && slipFront && mMovementStates.slipUpJumped) {
                    mMovementStates.wallSlipUp = true;
                    cutGravityElementFromJumpVec(true);

                    mJumpVec += -getAirGravityVec() * 30.0f;
                }
            }
        }
    } else if (!mMovementStates.binderHit && isRising() && !mMovementStates.frontWall && (mMovementStates.frontLedge)) {
        trySlipUpJump();
    }
}

void Mario::moveWallSlide(f32 rate) {
    Vec3f crossVec;
    const Vec3f& wallNorm = getWallNorm();
    crossVec.cross(*getGravityVec(), wallNorm);

    if (!MR::normalizeOrZero(&crossVec) && isStickOn()) {
        f32 dot = crossVec.dot(getWorldPadDir());
        const f32 absDot = MR::abs(dot);
        if (absDot > 0.2f) {
            if (dot < -0.2f) {
                dot = (0.2f + dot) / 0.8f;
            } else {
                dot = (dot - 0.2f) / 0.8f;
            }

            addVelocity(crossVec, (10.0f * dot) * rate);
        }
    }
}

bool Mario::jumpToHipDrop() {
    if (mMovementStates.hipDrop) {
        return false;
    }

    switch (mJumpKind) {
    case 5:
        return false;

    case 9:
    case 11:
    case 12:
    case 13:
        if (getPlayerMode() != 4 && isRising()) {
            return false;
        }
    }

    if (mVerticalSpeed < mActor->getConst().getTable()->mHipDropLimitHeight) {
        return false;
    }

    if (MR::isNearZero(mActor->mGravityVec)) {
        return false;
    }

    if (getPlayerMode() == 6) {
        return false;
    }

    bool isNormalDrop = false;
    if (mActor->mPunchSensorOn) {
        isNormalDrop = true;
    }

    resetTornado();
    cancelTornadoJump();

    mDrawStates.endTornado = true;
    mMovementStates.squatLocked = false;
    mMovementStates.hipDrop = true;
    mJumpVec.zero();
    mHipDropStillTimer = 0;

    if (mJumpKind == 4) {
        setFrontVecKeepUp(-mTurnFrontVec);
        mJumpKind = 0;
    }

    if (isAnimationRun("カリカリ限界")) {
        stopAnimationUpper(nullptr);
    }

    if (isPlayerModeHopper()) {
        mHipDropStartAnim = getAnimationStringPointer("ホッパーヒップドロップ開始");
        mHipDropAnim = getAnimationStringPointer("ホッパーヒップドロップ");
        mHipDropLandAnim = getAnimationStringPointer("ヒップドロップ着地");
        startHipDropBlur();
    } else if (getPlayerMode() == 4) {
        mHipDropStartAnim = getAnimationStringPointer("ハチヒップドロップ開始");
        mHipDropAnim = getAnimationStringPointer("ハチヒップドロップ");

        if (mActor->mBeeWallWalk != 0) {
            mHipDropLandAnim = getAnimationStringPointer("ハチヒップドロップ壁着地");
        } else {
            mHipDropLandAnim = getAnimationStringPointer("ハチヒップドロップ着地");
        }
    } else if (isNormalDrop) {
        mHipDropStartAnim = getAnimationStringPointer("スピンヒップドロップ開始");
        mHipDropAnim = getAnimationStringPointer("スピンヒップドロップ");
        mHipDropLandAnim = getAnimationStringPointer("スピンヒップドロップ着地");
        stopEffect("スピンライト");
        mActionStates.spinHipDrop = true;
    } else {
        mHipDropStartAnim = getAnimationStringPointer("ヒップドロップ開始");
        mHipDropAnim = getAnimationStringPointer("ヒップドロップ");
        mHipDropLandAnim = getAnimationStringPointer("ヒップドロップ着地");
        startHipDropBlur();
    }

    changeAnimation(mHipDropStartAnim, mHipDropAnim);
    playSound("尻ドロップ回転");

    if (isNormalDrop) {
        playSound("声スピン尻ドロップ");
        playSound("スピン尻ドロップ回転");

    } else {
        playSound("声尻ドロップ");
    }

    mZeroGravityTimer = mActor->getConst().getTable()->mHipDropZeroGrTime;
    mRabbit->forceJump();
    mMovementStates.launchKind = 0;
    return true;
}

void Mario::procHipDrop() {
    f32 gravityHipDrop;
    if (isAnimationRun(mHipDropStartAnim)) {
        if (!isAnimationTerminate(nullptr)) {
            return;
        }

        if (mZeroGravityTimer != 0) {
            mZeroGravityTimer--;
            return;
        }

        stopAnimation(nullptr);
        return;
    }

    if (mZeroGravityTimer != 0) {
        mZeroGravityTimer--;
        return;
    }

    if (mActor->mStunTimer != 0) {
        return;
    }

    if (mActor->mGravityJumpTimer != 0) {
        mHipDropJumpVec.x *= 0.5f;
        mHipDropJumpVec.y *= 0.5f;
        mHipDropJumpVec.z *= 0.5f;
        mJumpVec.x *= 0.5f;
        mJumpVec.y *= 0.5f;
        mJumpVec.z *= 0.5f;
    }

    if ((mMovementStates.onGround) != 0 || isAnimationRun(mHipDropLandAnim)) {
        fixFrontVecByGravity();

        if (isAnimationRun(mHipDropLandAnim)) {
            bool shouldEnd = false;

            if (mActor->mGravityJumpTimer != 0) {
                shouldEnd = true;
            } else if ((mMovementStates.onGround) == 0 && mAirTimer > 3 && mVerticalSpeed > 10.0f) {
                changeAnimation(mHipDropAnim);
                mJumpVec = mHipDropJumpVec;
                goto PROC_HIP_DROP_MOVE;
            }

            u16 endLimit = 0xF;
            if (isAnimationRun("スピンヒップドロップ着地")) {
                endLimit = 0x2D;
            }

            if (mLandTimer > endLimit) {
                if (isStickOn()) {
                    shouldEnd = true;
                }

                if (mDrawStates.onSlipFloor) {
                    shouldEnd = true;
                }
            }

            if (mLandTimer > 5 && checkTrgA()) {
                shouldEnd = true;
            }

            if (isAnimationTerminate(nullptr) || shouldEnd) {
                mMovementStates.jumping = false;
                mMovementStates.hipDrop = false;
                stopAnimation(nullptr, "基本");
                stopEffect("属性尻ドロップ");

                if (checkTrgA()) {
                    tryJump();
                    return;
                }
            }

            mWalkSpeed = 0.0f;
        } else {
            mLandTimer = 0;
            mWalkSpeed = 0.0f;
            mBrakeFirstTimer = 0;

            if (isCurrentFloorSand()) {
                changeStatus(mBury);
                return;
            }

            if (isCurrentFloorSink()) {
                mMovementStates.jumping = false;
                mMovementStates.hipDrop = false;
                stopAnimation(nullptr, "基本");
                mSinkTimer = 200;
                return;
            }

            playSound("尻ドロップ着地");
            playSound("声尻ドロップ着地");
            playEffectRT("属性尻ドロップ", mGroundNorm, mPosition);
            startPadVib("最強");
            startCamVib(0);
            mDrawStates.hipDropLand = true;
            MR::removeAllClingingKarikari();

            if (mFloorCode == 0x1B || mFloorCode == 0x1C || mFloorCode == 9) {
                mMovementStates.jumping = false;
                mMovementStates.hipDrop = false;
                stopAnimation(nullptr, "基本");
                return;
            }

            changeAnimationWithAttr(mHipDropLandAnim, 1);
            mHipDropJumpVec = mJumpVec;
            clearSlope();

            if (mActor->mAnimationFrozen) {
                mMovementStates.jumping = false;
                mMovementStates.hipDrop = false;
            }
        }

        mJumpVec.zero();
    } else {
        mAirTimer++;

        if (MR::isNearZero(mActor->getLastMove())) {
            mHipDropStillTimer++;
            if (mHipDropStillTimer == 0xF) {
                stopAnimation(nullptr, "基本");
                mMovementStates.jumping = false;
                mMovementStates.hipDrop = false;
                mJumpVec.zero();
                mMovementStates.onGround = true;
                return;
            }
        }
    }

PROC_HIP_DROP_MOVE:
    Vec3f moveVec(mJumpVec);
    if (moveVec.length() >= mVerticalSpeed) {
        moveVec.setLength(mVerticalSpeed);
    }

    addVelocity(moveVec);

    if (!mMovementStates.onGround) {
        const f32 speedRate = MR::clamp(0.1f + (1.0f - (mJumpVec.length() / mActor->getConst().getTable()->mLimitSpeedHipDrop)), 0.0f, 1.0f);
        gravityHipDrop = mActor->getConst().getTable()->mGravityHipDrop;

        mJumpVec += *getGravityVec() * gravityHipDrop * speedRate;

        const f32 jumpGravity = cutGravityElementFromJumpVec(true);
        if (mJumpVec.length() > 10.0f) {
            mJumpVec.x *= 0.5f;
            mJumpVec.y *= 0.5f;
            mJumpVec.z *= 0.5f;
        }

        mJumpVec += getAirGravityVec() * jumpGravity;

        if (mJumpVec.length() > mActor->getConst().getTable()->mLimitSpeedHipDrop) {
            mJumpVec.setLength(mActor->getConst().getTable()->mLimitSpeedHipDrop);
        }

        if (mActionStates.spinHipDrop && isAnimationRun("スピンヒップドロップ")) {
            playSound("スピン尻ドロップ落下");

            HitSensor* homingSensor = nullptr;

            f32 maxAngle = 45.0f;
            f32 speed = mVerticalSpeed;
            if (speed >= 500.0f) {
                speed = 500.0f;
            }

            s32 strikeNum = Collision::checkStrikeBallToMapWithThickness(mShadowPos, speed, speed, nullptr, nullptr);

            for (u32 i = 0; i < strikeNum; i++) {
                const HitInfo* strikeInfo = Collision::getStrikeInfoMap(i);

                if (mActor->selectHomingInSuperHipDrop(strikeInfo->mParentTriangle.mSensor->mHost->mName)) {
                    Vec3f sensorPos(strikeInfo->mParentTriangle.mSensor->mPosition);

                    if (MR::diffAngleAbs(sensorPos - mActor->mCenterPos, *getGravityVec()) < maxAngle) {
                        homingSensor = strikeInfo->mParentTriangle.mSensor;
                    }
                }
            }

            if (homingSensor == nullptr) {
                homingSensor = reinterpret_cast< HitSensor* >(mActor->mHomingSensor);
            }

            if (homingSensor != nullptr) {
                Vec3f targetJump(homingSensor->mPosition - mActor->mCenterPos);
                targetJump.setLength(mJumpVec.length());

                Vec3f lastMove;
                mActor->getLastMove(&lastMove);
                if (lastMove.dot(mJumpVec) < 0.0f) {
                    const f32 hitGravity = cutGravityElementFromJumpVec(true);
                    mJumpVec = -mJumpVec + getAirGravityVec() * hitGravity;
                } else if (mJumpVec.dot(targetJump) > 0.0f) {
                    mJumpVec = targetJump;
                }
            }

            if ((mMovementStates.frontWall) || (mMovementStates.sideWall) || (mMovementStates.backWall)) {
                mActionStates.spinHipDrop = false;
            }
        }

        fixFrontVecFromUpSide();
    }
}

void Mario::doAirWalk() {
    if (isAnimationRun("水泳ジェット")) {
        return;
    }

    if (mJumpKind == 3) {
        return;
    }

    if (mMovementStates.forcedLaunch) {
        return;
    }

    if (isAnimationRun("壁はじき")) {
        return;
    }

    if (mMovementStates.waitStickNeutral) {
        if (mAirTimer > mActor->getConst().getTable()->mWaitNeutralTimer) {
            mMovementStates.waitStickNeutral = false;
        }

        return;
    }

    if (mAirControlInhibitTimer != 0) {
        mAirControlInhibitTimer--;
        if (mAirControlInhibitTimer == 0) {
            stopAnimation("坂すべり上向きうつぶせ", MarioIndexedAnime_SlideUpFaceDown);
            stopAnimation("坂すべり下向きあおむけ", MarioIndexedAnime_SlideDownFaceUp);
        }

        return;
    }

    f32 speedKiller;
    Vec3f moveDir;
    Vec3f sideMove;
    calcMoveDir(mStickPos.x, mStickPos.y, &moveDir, true);

    if (mActor->mGravityJumpTimer != 0) {
        moveDir.zero();
    }

    if (mDrawStates.moveDirKept) {
        if (MR::diffAngleAbs(getAirGravityVec(), mJumpGravityVec) > 0.7853982f) {
            Vec3f sideVec;
            sideVec.cross(mFrontVec, getAirGravityVec());
            MR::normalizeOrZero(&sideVec);

            Mtx rotMtx;
            NebulaMtxRotAxisRad(rotMtx, &sideVec, MR::diffAngleSignedHorizontal(mJumpGravityVec, getAirGravityVec(), sideVec));

            Vec3f frontRot;
            NebulaMtxMultVecSR(rotMtx, &mJumpFrontVec, &frontRot);
            if (frontRot.dot(mFrontVec) < 0.0f) {
                moveDir = -mFrontVec;
            } else {
                moveDir = mFrontVec;
            }
        }
    }

    MR::vecKillElement(moveDir, *getGravityVec(), &moveDir);

    bool inhibitFrontAdjust = false;
    f32 frontDot = MR::vecKillElement(moveDir, mFrontVec, &sideMove);

    if (mJumpKind == 0xB || mJumpKind == 8 || getPlayerMode() == 4 || getPlayerMode() == 6) {
        inhibitFrontAdjust = true;
    } else if (frontDot >= 0.0f) {
        f32 frontReduction = mActor->getConst().getTable()->mJumpFrontReduction;
        s16 reduceBegin = mActor->getConst().getTable()->mJumpFrontReductionBeginTime;
        if (mJumpKind == 5) {
            frontReduction = mActor->getConst().getTable()->mSquatJumpFrontReduction;
            reduceBegin = mActor->getConst().getTable()->mSquatJumpFrontReductionBTime;
        }

        if (mActionStates.jumpOnEnforcer && mJumpEnforceSensor == mShadowPolygon->mSensor) {
            reduceBegin = 0xB4;
            if (MR::isSameMtx(mJumpEnforcePolygon->getBaseMtx()->toMtxPtr(), mJumpEnforcePolygon->getPrevBaseMtx()->toMtxPtr())) {
                MR::vecKillElement(mEnforceJumpVec, getAirGravityVec(), &mEnforceJumpVec);
                mJumpVec -= mEnforceJumpVec;
                mEnforceJumpVec.zero();
            }
        }

        if ((mMovementStates.beeFly) == 0 && mAirTimer > reduceBegin) {
            const f32 jumpGravity = cutGravityElementFromJumpVec(true);
            const f32 jumpMag = mJumpVec.length();
            mJumpVec.setLength(frontReduction * jumpMag);

            mJumpVec += getAirGravityVec() * jumpGravity;
        }
    } else {
        if (!isRising() && mVerticalSpeed < mActor->getConst().getTable()->mLandTurnHeight && (mMovementStates.beeFly) == 0) {
            mMovementStates.turnStickNeutral = true;
        } else {
            mBackJumpTimer++;
        }

        if (isAnimationRun("ターンジャンプ")) {
            mBackJumpTimer = 0;
            speedKiller = 0.1f;
        } else if (mBackJumpTimer < mActor->mConst->getTable()->mBackJumpLimitFrame) {
            const f32 remain = static_cast< f32 >(mActor->getConst().getTable()->mBackJumpLimitFrame - mBackJumpTimer);
            speedKiller =
                mActor->getConst().getTable()->mBackJumpRatio * (remain / static_cast< f32 >(mActor->getConst().getTable()->mBackJumpLimitFrame));
        } else {
            speedKiller = 0.0f;
        }

        if (mMovementStates.beeFly) {
            moveDir = sideMove + mFrontVec * mActor->getConst().getTable()->mAirWalkBackBonus * frontDot;
        } else {
            moveDir = sideMove + mFrontVec * frontDot * speedKiller;
        }

        const f32 backDot = -moveDir.dot(mFrontVec);
        if (backDot > mActor->getConst().getTable()->mMaxBackJumpSpeed) {
            moveDir.setLength(mActor->getConst().getTable()->mMaxBackJumpSpeed);
        } else if (backDot < 0.0f && mBackJumpTimer < mActor->mConst->getTable()->mBackJumpLimitFrame) {
            mBackAirWalkTimer = 0xA;
        }
    }

    Vec3f moveNorm(moveDir);
    MR::normalizeOrZero(&moveNorm);

    Vec3f jumpNorm(mJumpVec);
    MR::normalizeOrZero(&jumpNorm);

    if (!inhibitFrontAdjust) {
        f32 alignment = moveNorm.dot(jumpNorm);
        if (alignment < 0.0f) {
            const f32 jumpGravity = cutGravityElementFromJumpVec(true);
            mJumpVec += mJumpVec * mActor->getConst().getTable()->mAirWalkSpeedKiller * alignment;

            mJumpVec += getAirGravityVec() * jumpGravity;
        }
    }

    if (mJumpKind == 8) {
        mJumpVec += moveDir * mActor->getConst().getTable()->mWalkSpeed * 5.0f / mActor->getConst().getTable()->mAirWalkTimerFact2;
    } else if (mJumpKind == 0xB && !isRising()) {
        mJumpVec += moveDir * mActor->getConst().getTable()->mWalkSpeed * 5.0f / mActor->getConst().getTable()->mAirWalkTimerFact2;
        playSound("滞空中");
    } else if (getPlayerMode() == 4) {
        mJumpVec += moveDir * mActor->getConst().getTable()->mBeeAirWalkAcc;
        playSound("滞空中");
    } else if (getPlayerMode() == 6) {
        if (mJumpVec.dot(moveDir) >= 0.0f) {
            f32 frontGravity = moveDir.dot(mFrontVec);
            if (frontGravity > 0.0f) {
                frontGravity = -frontGravity;
            }

            Vec3f normJump(mJumpVec);
            if (!MR::normalizeOrZero(&normJump)) {
                f32 alignment = normJump.dot(mFrontVec);
                alignment += 1.0f;
                f32 halfAlignment = alignment / 2.0f;
                mJumpVec *= 0.998f - 0.01f * (1.0f - halfAlignment);
            }
        }
    } else {
        mJumpVec += moveDir * mActor->getConst().getTable()->mWalkSpeed * mActor->getConst().getTable()->mAirWalkTimerFact1 /
                    (mAirTimer + mActor->getConst().getTable()->mAirWalkTimerFact2);
    }

    f32 accelerationRate = 1.0f;
    if (mJumpKind != 0xB && getPlayerMode() != 6 && !isRising()) {
        addVelocity(moveDir, mActor->getConst().getTable()->mWalkSpeed * mActor->getConst().getTable()->mAirWalkTimerFact3 * accelerationRate);
    }

    const f32 jumpGravity = cutGravityElementFromJumpVec(true);
    if (mJumpKind == 8) {
        if (mJumpVec.length() > 6.0f) {
            mJumpVec.setLength(6.0f);
        }
    } else if (mJumpKind == 0xB) {
        if (mJumpVec.length() > 7.0f) {
            mJumpVec.setLength(7.0f);
        }
    } else if (getPlayerMode() == 4) {
        if (mBeeFlyInhibitTimer == 0) {
            if (mJumpVec.length() > mActor->getConst().getTable()->mBeeAirWalkLimit) {
                mJumpVec.setLength(mActor->getConst().getTable()->mBeeAirWalkLimit);
            }
        } else if (mBeeFlyInhibitTimer > 0x1E) {
            if (mJumpVec.length() > mActor->getConst().getTable()->mMaxJumpSpeed) {
                mJumpVec.setLength(mActor->getConst().getTable()->mMaxJumpSpeed);
            }
        } else {
            f32 t = static_cast< f32 >(mBeeFlyInhibitTimer) / 30.0f;
            const f32 limit = (t * mActor->mConst->getTable()->mMaxJumpSpeed) + ((1.0f - t) * mActor->getConst().getTable()->mBeeAirWalkLimit);
            if (mJumpVec.length() > limit) {
                mJumpVec.setLength(limit);
            }
        }
    } else if (getPlayerMode() != 6) {
        if (mJumpVec.length() > mActor->getConst().getTable()->mMaxJumpSpeed) {
            mJumpVec.setLength(mActor->getConst().getTable()->mMaxJumpSpeed);
        }
    }

    mJumpVec += getAirGravityVec() * jumpGravity;
}

void Mario::stopJump() {
    fixFrontVecByGravity();
    mJumpVec.zero();

    mMovementStates.jumping = false;
    mMovementStates.hipDrop = false;
    mMovementStates.wallSlipUp = false;

    mMovementStates.spinBlocked = false;

    mActionStates.jumpOnEnforcer = false;
    mActionStates.spinJumpLand = false;
    mActionStates.jump2P = false;

    if (getPlayerMode() != 4) {
        mAirWalkTimer = mActor->getConst().getTable()->mAirWalkTime;
    }

    mAirTimer = 0;
    mZeroGravityTimer = 0;
    mTornadoBoostTimer = 0;
    mAirControlInhibitTimer = 0;

    if (isDefaultAnimationRun("落下")) {
        changeAnimation(nullptr, "基本");
    }

    if (isDefaultAnimationRun(mHipDropAnim)) {
        changeAnimation(nullptr, "基本");
    }

    mWall->mCancel = 0;
}

void Mario::cancelTornadoJump() {
    mJumpKind = 0;

    if ((mMovementStates.onGround) == 0) {
        stopAnimation("スピンジャンプ", "落下");
    } else {
        stopAnimation("スピンジャンプ");
    }

    mJumpStartPos = mPosition;
    mMovementStates.spinBlocked = true;
    mTornadoBoostTimer = 0;
}

void Mario::setRocketBooster(const Vec3f& rVec, f32 decay, u16 frames) {
    mTornadoBoostTimer = frames;
    mRocketBoostDecay = decay;
    mRocketBoostVec = rVec;
}

void Mario::procRocketBooster() {
    if (mTornadoBoostTimer == 0) {
        return;
    }

    mTornadoBoostTimer--;
    mJumpVec += mRocketBoostVec;
    mRocketBoostVec.scale(mRocketBoostDecay);
}

bool Mario::isSoftLandingFloor() const {
    switch (mFloorCode) {
    case 0x6:
    case 0x7:
    case 0x8:
    case 0xB:
    case 0x22:
        return true;
    default:
        return false;
    }
}

void Mario::checkAndTryForceJump() {
    bool doForceJump = false;
    Vec3f forceJumpVec;

    if ((getMovementStates().onGround && !mMovementStates.jumping) || isAnimationRun(mHipDropLandAnim)) {
        switch (mFloorCode) {
        case 0x6:
        case 0x22: {
            const f32 jumpPower = mActor->getConst().getTable()->mCodeJumpPower[0];
            forceJumpVec = *mGroundPolygon->getNormal(0) * jumpPower;
            doForceJump = true;
            break;
        }

        case 0x7: {
            const f32 jumpPower = mActor->getConst().getTable()->mCodeJumpPower[1];
            forceJumpVec = *mGroundPolygon->getNormal(0) * jumpPower;
            doForceJump = true;
            break;
        }

        case 0x8: {
            const f32 jumpPower = mActor->getConst().getTable()->mCodeJumpPower[2];
            forceJumpVec = *mGroundPolygon->getNormal(0) * jumpPower;
            doForceJump = true;
            break;
        }

        case 0xB: {
            MR::vecKillElement(mPrevVelocity, *getGravityVec(), &forceJumpVec);
            forceJumpVec.setLength(10.0f);

            const f32 jumpPower = mActor->getConst().getTable()->mCodeJumpPower[3];
            forceJumpVec += -(*getGravityVec()) * jumpPower;
            doForceJump = true;
            break;
        }

        default:
            break;
        }
    }

    if (mMovementStates.forceJumpDelay) {
        doForceJump = false;

        if (mActor->mHealth != 0) {
            if (getCurrentStatus() == 2) {
                closeStatus(nullptr);
            }

            tryForceFreeJump(mForceJumpVec);
        }

        mMovementStates.forceJumpDelay = false;
    }

    if (mMovementStates.freeJumpDelay) {
        doForceJump = false;

        if (mActor->mHealth != 0) {
            if (getCurrentStatus() == 2) {
                closeStatus(nullptr);
            }

            tryFreeJump(mForceJumpVec, true);
        }

        mMovementStates.freeJumpDelay = false;
    }

    if (doForceJump) {
        mActor->sendMsgToSensor(mGroundPolygon->mSensor, 2);

        if (getCurrentStatus() == 2) {
            if (mActor->mHealth == 0) {
                mActor->forceGameOverNonStop();
            }

            closeStatus(nullptr);
        }

        mMovementStates.squatLocked = true;
        mMovementStates.freeJumpDelay = true;
        mForceJumpVec = forceJumpVec;
        changeAnimation("フリージャンプ");

        if (mActor->isEnableNerveChange()) {
            playSound("トランポリンジャンプ大");
            playSound("声大ジャンプ");
            playSound("ジャンプ踏切");
        }
    }
}

void Mario::doLanding() {
    bool doHardLanding = false;
    bool blendWait = false;
    bool keepFrontSlip = false;

    if (getPlayerMode() == 6) {
        return;
    }

    if (mMovementStates.mode2D) {
        afterLanding2D();
    }

    mMovementStates.jumping = false;
    mActionStates.jump2P = false;
    mActionStates.fooFlying = false;
    mLandTimer = 0;
    mHitStates.landed = true;

    f32 deltadot = MR::abs((mPosition - mJumpStartPos).dot(*getGravityVec()));
    if (mAirTimer <= 3 && deltadot < 1.0f && mJumpVec.length() < 10.0f) {
        stopAnimation(nullptr, "基本");
        return;
    }

    if (mActor->mSwingPermission && (mMovementStates.spinBlocked) && mActionStates.spinJumpLand && mJumpKind != 3) {
        mActionStates.spinJumpLand = false;
        mActor->mSpinIntervalTimer = 0x10;
    }

    resetTornado();

    if (mActor->mBeeWallWalk != 0) {
        playSound("ハチ壁くっつき");
    } else {
        playSound("着地");
    }

    if ((isAnimationRun("スケートアクセルジャンプ") || isAnimationRun("スケートジャンプ2") || isAnimationRun("スケートジャンプ3")) &&
        (getPlayerMode() == 3 || getPlayerMode() == 0)) {
        playEffect("スケート着地");
        if (getStickP() == 0.0f) {
            mJumpVec.zero();
            stopWalk();
            changeAnimation("スケート静止着地", "基本");
        } else {
            changeStatus(mSkate);
        }

        return;
    }

    if (mJumpKind == 5 && isSkatableFloor()) {
        if (checkSquat(false)) {
            mMovementStates.squat = true;
            mWalkSpeed = 1.0f;
            stopAnimation(nullptr, "しゃがみ基本");
        } else {
            cancelSquatMode();
            stopAnimationUpper(nullptr);
            changeAnimation("スケート静止着地", "基本");
        }

        return;
    }

    if (isAnimationRun("フーファイター着地")) {
        stopAnimation(nullptr, "基本");
        return;
    }

    if (getPlayer()->mActionStates.recoveryLand) {
        getPlayer()->mActionStates.recoveryLand = false;
        mJumpKind = 0xE;
    } else {
        const u32 landingType = mMovementStates.launchKind;
        if (landingType == 2) {
            getPlayer()->mMovementStates.launchKind = 0;
            stopWalk();
            playSound("重い着地");
            playSound("声ステージイン着地");
            changeAnimation("ステージインB", "基本");
            startCamVib(4);
            return;
        }

        if (landingType == 1) {
            getPlayer()->mMovementStates.launchKind = 0;
            playEffect("スーパースピンドライバ終了");
            playEffect("共通着地大");
            if (!isStickFull()) {
                keepFrontSlip = true;
            }

            doHardLanding = true;
            if (mPostureUpVec.dot(mGroundNorm) < 0.0f) {
                if (mFloorCode == 0xD || mFloorCode == 0x1E || mFloorCode == 5 || mFloorCode == 0x17) {
                    changeStatus(mSukekiyo);
                } else {
                    stopWalk();
                    startCamVib(4);
                    changeAnimation("ハード着地", "基本");
                    mLandTimer += 0x10;
                    forceSetHeadVecKeepSide(mGroundNorm);
                }

                return;
            }
        } else {
            if (isCurrentFloorSink()) {
                stopAnimation(nullptr, "基本");
                mSinkTimer = 64;
                return;
            }

            if ((mFloorCode == 0x1B || mFloorCode == 0x1C)) {
                if (MR::getSoundCodeIndex(mShadowPolygon) == CollisionSoundCode_Sand) {
                    stopAnimation(nullptr, "基本");
                    mSinkTimer = 32;
                    return;
                }
            } else if (mAirTimer > 10) {
                playEffect("共通着地普通");
            }
        }
    }

    switch (mJumpKind) {
    case 0:
    case 0xA:
    case 0xB:
    case 0xE:
        startPadVib(static_cast< u32 >(0));
        break;
    default:
        startPadVib(2);
        break;
    case 2:
    case 4:
    case 6:
        startPadVib(3);
        break;
    }

    getAnimator()->addRumblePower(5.0f, 0x3C);
    mMovementStates.squatLocked = false;

    if (getPlayerMode() != 4) {
        mAirWalkTimer = mActor->getConst().getTable()->mAirWalkTime;
    } else {
        mBrakeFirstTimer = 0;
    }

    if (!isSoftLandingFloor()) {
        f32 deltadot = (mJumpStartPos - mPosition).dot(-(*getGravityVec()));
        if (deltadot > mActor->getConst().getTable()->mHardLandingHeight) {
            doHardLanding = true;
        }
    }

    if (doHardLanding && isCurrentFloorSand()) {
        changeStatus(mBury);
        return;
    }

    if (isBlendWaitGround()) {
        blendWait = true;
    }

    fixFrontVecByGravity();

    if (!doHardLanding && !mDrawStates.onSlipFloor && mJumpKind != 0xE && !isAnimationRun("飛び込みジャンプ") && !isAnimationRun("後方飛び込みジャンプ") &&
        !mActionStates.rollLanding && !isAnimationRun("水上ダメージ中")) {
        if (mJumpKind == 5 && checkSquat(false)) {
            mMovementStates.squat = true;
            mWalkSpeed = 1.0f;
            stopAnimation(nullptr, "しゃがみ基本");
            goto POST_LANDING;
        }

        if (((isStickFull() || mCameraTurnHold) && mBackJumpTimer == 0) || (((mMovementStates.hipDrop) == 0) && checkSquat(false))) {
            stopAnimation(nullptr, "基本");
            if (!mActionStates.retainInAir) {
                recordTurnSlipAngle();
            }

            if (mJumpKind == 5) {
                cancelSquatMode();
            }

            goto POST_LANDING;
        }

        switch (mJumpKind) {
        case 2:
        case 4:
        case 5:
        case 6:
        case 8:
        case 0xB:
            keepFrontSlip = true;
            break;
        default:
            break;
        }

        if (mActionStates.jumpOnEnforcer && mJumpEnforceSensor == mShadowPolygon->mSensor) {
            Vec3f jumpDelta(mJumpVec - mEnforceJumpVec);
            if (jumpDelta.dot(mJumpVec) < 0.0f) {
                mJumpVec.zero();
            } else {
                mJumpVec = jumpDelta;
            }
        }

        const f32 frontDot = mFrontVec.dot(mJumpVec);
        if (frontDot > 6.0f && !keepFrontSlip) {
            mWalkSpeed = frontDot / mActor->getConst().getTable()->mJumpFrontSpeed;
            mWalkSpeed = MR::clamp(mWalkSpeed, 0.0f, 2.0f);
            stopAnimation(nullptr, "基本");
            mStartSpinTimer = 0;
            mBrakeFirstTimer = 0;

            if (!mActionStates.retainInAir) {
                recordTurnSlipAngle();
            }

            goto POST_LANDING;
        }
    }

    if (mJumpKind == 4) {
        setFrontVecKeepUp(-mTurnFrontVec);
    }

    if (isAnimationRun("飛び込みジャンプ") || isAnimationRun("後方飛び込みジャンプ") || mActionStates.rollLanding) {
        if (isAnimationRun("後方飛び込みジャンプ")) {
            setFrontVecKeepUp(-mFrontVec);
        }

        if (mActionStates.rollLanding) {
            doHardLanding = true;
        }

        stopWalk();
        if (doHardLanding || isAnimationRun("後方飛び込みジャンプ")) {
            if (doHardLanding) {
                changeAnimation("飛び込み失敗回転着地", "基本");
                startSlidingTask(8, 5.0f, 0x19);
                startCamVib(0);
            } else {
                changeAnimation("飛び込み失敗着地", "基本");
            }
        } else {
            changeAnimation("飛び込み失敗着地", "基本");
        }

        goto POST_LANDING;
    }

    if (!mDrawStates.onSlipFloor) {
        clearSlope();
        switch (mJumpKind) {
        case 0:
        case 7:
            changeAnimation("着地", "基本");
            break;
        case 1:
            changeAnimation("着地B", "基本");
            break;
        case 2:
            changeAnimation("着地C", "基本");
            break;
        case 4:
            changeAnimation("着地ターン", "基本");
            break;
        case 5:
            if (!checkSquat(false)) {
                cancelSquatMode();
            }

            if (mActor->mTakeCount == 0) {
                stopAnimationUpper(nullptr);
            }

            changeAnimation("着地幅とび", "基本");
            break;
        case 6:
            changeAnimation("しゃがみジャンプ着地", "基本");
            break;
        default:
            if (isAnimationRun("水上ダメージ中")) {
                changeAnimation("中ダメージ着地", "基本");
            } else {
                changeAnimation("着地", "基本");
            }

            break;
        case 3:
        case 0xA:
            stopAnimation(nullptr, "基本");
            break;
        case 0xE:
            playSound("倒れ");
            playSound("着地");
            playSound("声小ダメージ");
            playEffect("共通引き戻し着地");
            changeAnimation("引き戻し着地", "基本");
            doHardLanding = false;
            blendWait = false;
            break;
        }

        stopWalk();
        if (blendWait) {
            changeAnimation("ショート着地", "基本");
        }

        if (doHardLanding) {
            changeAnimation("ハード着地", "基本");
            playSound("重い着地");
            playSound("声着地停止");
            startCamVib(4);
            mLandTimer += 0x10;
        }

        if (mActor->mBeeWallWalk != 0) {
            changeAnimation("ハチ匍匐着地", "基本");
        }
    } else {
        if (!isAnimationRun("坂すべり上向きうつぶせ", MarioIndexedAnime_SlideUpFaceDown) && !isAnimationRun("坂すべり下向きあおむけ", MarioIndexedAnime_SlideDownFaceUp)) {
            stopAnimation(nullptr, "基本");
        }

        changeAnimation("すべり着地", "基本");
        mBrakeFirstTimer = 0;
    }

POST_LANDING:
    if (mDrawStates.onSlipFloor) {
        Vec3f jumpNoGrav;
        MR::vecKillElement(mJumpVec, *getGravityVec(), &jumpNoGrav);

        if (jumpNoGrav.dot(*mGroundPolygon->getNormal(0)) > 0.0f) {
            mMovementStates.slopeSlide = true;
            mSlipPower = 10.0f;
        } else {
            const f32 ratio = (90.0f - calcPolygonAngleD(mGroundPolygon)) / 90.0f;
            mWalkSpeed = mWalkSpeed * ratio;
            mWalkSpeed = MR::clamp(mWalkSpeed, 0.0f, 2.0f);

            const f32 minVel = 10.0f - (10.0f * ratio);
            if (mSlipPower < minVel) {
                mSlipPower = minVel;
            }
        }

        if (jumpNoGrav.length() < mSlopeMoveVec.length()) {
            mSlopeMoveVec.setLength(0.5f * (mSlopeMoveVec.length() + jumpNoGrav.length()));
        }
    } else {
        mSlopeMoveVec.zero();
    }

    return;
}

void Mario::startSlidingTask(u32 taskFlags, f32 speed, u16 frames) {
    mSlideVec = mFrontVec * speed;
    mSlideTimer = frames;
    pushTask(reinterpret_cast< Task >(&Mario::taskOnSlide), taskFlags);
}

bool Mario::taskOnSlide(u32) {
    addVelocity(mSlideVec);
    mSlideTimer--;
    return mSlideTimer != 0;
}

void Mario::incAirWalkTimer() {
    const u16 timer = mAirWalkTimer;
    const u16 limit = mActor->getConst().getTable()->mAirWalkTime;

    if (timer >= limit) {
        return;
    }

    mAirWalkTimer = timer + 1;

    if (getPlayerMode() != 4) {
        return;
    }

    const u16 nextTimer = mAirWalkTimer;
    const u16 nextLimit = mActor->getConst().getTable()->mAirWalkTime;

    if (nextTimer >= nextLimit) {
        playSound("ハチ体力完全回復");
    } else {
        playSound("ハチ体力回復");
    }
}
