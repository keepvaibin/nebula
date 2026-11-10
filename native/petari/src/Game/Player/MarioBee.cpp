#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioShadow.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_beeMarioOnAir_f1, "beeMarioOnAir.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_beeMarioOnAir_f2, "beeMarioOnAir.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_beeMarioOnAir_f3, "beeMarioOnAir.f3", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_beeMarioOnAir_f4, "beeMarioOnAir.f4", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_beeMarioOnAir_f5, "beeMarioOnAir.f5", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_beeMarioOnAir_f6, "beeMarioOnAir.f6", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_beeMarioOnAir_f7, "beeMarioOnAir.f7", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_beeMarioOnAir_f8, "beeMarioOnAir.f8", 0.5f);

void Mario::beeMarioOnGround() {
    if (getPlayerMode() == PlayerMode_Bee && !mDrawStates.onSlipFloor && mMovementStates.onGround && !mMovementStates.slopeSlide && !isStatusActive(MarioStatus_Slider)) {
        getPlayer()->incAirWalkTimer();
        getPlayer()->incAirWalkTimer();
        getPlayer()->incAirWalkTimer();
        getPlayer()->incAirWalkTimer();
    }
}

void MarioActor::entryWallWalkMode(const Vec3f& position, const Vec3f& normal) {
    if (mBeeWallWalk) {
        return;
    }

    if (!mBeeStickInhibit) {
        mBeeWallWalk = 5;
        Vec3f gravity(-normal);
        mGravityVec = gravity;
        mPosition = position;
        mMario->setTrans(position);
        mMario->stopJump();
        mMario->stopAnimation(nullptr);
        mMario->stopWalk();
        Vec3f front(mMario->mHeadVec);
        mMario->setGravityVec(gravity);
        mMario->setHeadVec(-gravity);
        mMario->setFrontVecKeepUp(front, 1u);
        setBlendMtxTimer(2);
        mStunTimer = 5;
        mMario->mMovementStates.jumpBuffered = false;
        mCollisionShadow->mSnapDirection = true;
    }
}

bool Mario::beeMarioOnAir() {
    if (mBeeFlyInhibitTimer) {
        mBeeFlyInhibitTimer--;
        if (!mBeeFlyInhibitTimer) {
            mMovementStates.forcedLaunch = false;
        }
    }

    if (getPlayerMode() == PlayerMode_Bee) {
        if (mAirTimer == 1 && mAirWalkTimer) {
            mAirWalkTimer--;
        }

        if (isAnimationRun("ハチ壁ジャンプ") && isAnimationTerminate(nullptr)) {
            if (checkLvlA()) {
                changeAnimation("ハチ飛行中", "落下");
            } else {
                changeAnimation("ハチ飛行中無入力", "落下");
            }

            changeAnimationInterpoleFrame(30);
        }
    }

    if (getPlayerMode() == PlayerMode_Bee) {
        Vec3f horizontal;
        if (MR::vecKillElement(mJumpVec, getAirGravityVec(), &horizontal) > -cNum_beeMarioOnAir_f1 || mBeeGravityReviveTimer) {
            if (!mBeeFlyInhibitTimer) {
                f32 verticalSpeed = cutGravityElementFromJumpVec(true);
                f32 speed = mJumpVec.length();
                MR::normalizeOrZero(&mJumpVec);
                if (!MR::vecBlendSphere(mJumpVec, mFrontVec, &mJumpVec, mActor->getConst().getTable()->mBeeSpeedRotateRatio)) {
                    Mtx rotation;
                    NebulaMtxRotAxisRad(rotation, &mHeadVec, cNum_beeMarioOnAir_f2);
                    NebulaMtxMultVecSR(rotation, &mJumpVec, &mJumpVec);
                }

                mJumpVec.setLength(speed);
                mJumpVec += getAirGravityVec() * verticalSpeed;
            }

            if (!mMovementStates.beeFly && mAirWalkTimer) {
                if (mBeeGravityReviveTimer < 30) {
                    mBeeGravityReviveTimer = 30;
                    mBeeUpDownSpeed = 0.0f;
                }

                mJumpStartPos = mPosition;
                mMovementStates.beeFly = true;
            }

            s16 inhibitTime = mActor->getConst().getTable()->mBeeAirWalkInhibitTime;
            s16 powerTime = mActor->getConst().getTable()->mBeeGravityPowerTime;
            Vec3f horizontalVelocity;
            MR::vecKillElement(mJumpVec, getAirGravityVec(), &horizontalVelocity);
            if (horizontalVelocity.length() < cNum_beeMarioOnAir_f3) {
                inhibitTime = mActor->getConst().getTable()->mBeeAirWalkInhibitTimeV;
                powerTime = mActor->getConst().getTable()->mBeeGravityPowerTimeV;
            }

            if (checkLvlA() && mAirWalkTimer && mAirTimer > inhibitTime) {
                playSound("ハチ飛行中");
                if (!MR::isNearZero(mStickPos.z)) {
                    setFrontVecKeepUp(getWorldPadDir(), mActor->getConst().getTable()->mBeeAirWalkTurnSpd);
                }

                u16 previousTime = mAirWalkTimer;
                if (mAirWalkTimer) {
                    if (!mMovementStates.tornado && mAirWalkTimer > mActor->getConst().getTable()->mAirWalkTime) {
                        mAirWalkTimer = mActor->getConst().getTable()->mAirWalkTime;
                    }

                    mAirWalkTimer--;
                }

                if (!mAirWalkTimer) {
                    if (previousTime) {
                        playSound("ハチ体力切れ");
                    }

                    mMovementStates.beeFly = false;
                    stopAnimation("ハチ飛行中");
                } else {
                    if (!mMovementStates.tornado) {
                        if (!isAnimationRun("ハチ壁ジャンプ") && !isAnimationRun("ハチスピン空中")) {
                            changeAnimation("ハチ飛行中", "落下");
                        }

                        cancelSquatMode();
                        playSound("空中ふんばり");
                        if (mAirWalkTimer < mActor->getConst().getTable()->mAirWalkTime / 2) {
                            getAnimator()->setSpeed(cNum_beeMarioOnAir_f4);
                        }

                        if (mJumpKind == 4) {
                            setFrontVecKeepUp(-mTurnFrontVec);
                            mJumpKind = 0;
                        }

                        if (mJumpKind == 5) {
                            mJumpKind = 0;
                        }
                    }

                    s16 time = static_cast< s16 >(mBeeGravityTimer);
                    if (time > powerTime) {
                        time = powerTime;
                    }

                    f32 ratio = static_cast< f32 >(time) / powerTime;
                    ratio = cNum_beeMarioOnAir_f5 * (ratio * ratio);
                    f32 acceleration = cNum_beeMarioOnAir_f6 * (MR::getRandom() - ratio);
                    acceleration =
                        acceleration * mActor->getConst().getTable()->mBeeFlyRandomFactor - mActor->getConst().getTable()->mBeeFlyConstantFactor;
                    f32 verticalSpeed = cutGravityElementFromJumpVec(true);
                    Vec3f velocity(mJumpVec);
                    f32 factor = 1.0f;
                    if (!MR::isNearZero(mStickPos.z)) {
                        velocity.dot(getWorldPadDir());
                    }

                    if (factor < 0.0f) {
                        factor *= mActor->getConst().getTable()->mBeeUpAccelRatio;
                    }

                    mBeeUpDownSpeed += factor * (acceleration * mActor->getConst().getTable()->mBeeAccelRatio);
                    if (mBeeUpDownSpeed > 0.0f) {
                        mBeeUpDownSpeed *= mActor->getConst().getTable()->mBeeUpDownKiller;
                    }

                    if (!mBeeFlyInhibitTimer) {
                        if (!getPlayer()->mHitStates.karikariClinging) {
                            if (mBeeUpDownSpeed < -mActor->getConst().getTable()->mBeeUpSpeedMax) {
                                mBeeUpDownSpeed = -mActor->getConst().getTable()->mBeeUpSpeedMax;
                            }
                        } else if (mBeeUpDownSpeed < -cNum_beeMarioOnAir_f7) {
                            mBeeUpDownSpeed = -cNum_beeMarioOnAir_f8;
                        }
                    }

                    addVelocity(*getGravityVec(), mBeeUpDownSpeed);
                    if (!getPlayer()->mHitStates.karikariClinging && verticalSpeed > 0.0f) {
                        verticalSpeed *= mActor->getConst().getTable()->mBeePushRiseGravityEraser;
                    }

                    mJumpVec += getAirGravityVec() * verticalSpeed;
                    mBeeGravityTimer++;
                    if (mBeeGravityTimer > 120) {
                        mBeeGravityTimer = 120;
                    }

                    mJumpStartPos = mPosition;
                    return true;
                }
            } else {
                if (!isAnimationRun("ハチジャンプ") && !isAnimationRun("ハチ壁ジャンプ")) {
                    stopAnimation("ハチ飛行中");
                    if (!isAnimationRun(nullptr) || isAnimationTerminate(nullptr)) {
                        changeAnimation("ハチ飛行中無入力");
                    }
                }

                if (mBeeGravityTimer) {
                    mBeeGravityTimer--;
                }

                if (mBeeUpDownSpeed < mActor->getConst().getTable()->mBeeFreeDropMaxSpd) {
                    mBeeUpDownSpeed += mActor->getConst().getTable()->mBeeFreeDropAcc;
                }

                f32 factor = 1.0f;
                addVelocity(*getGravityVec(), mBeeUpDownSpeed * factor);
                if (!MR::isNearZero(mStickPos.z)) {
                    setFrontVecKeepUp(getWorldPadDir(), mActor->getConst().getTable()->mBeeAirWalkTurnSpd);
                }
            }
        }
    }

    return false;
}
