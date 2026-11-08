#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioShadow.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MathUtil.hpp"

void Mario::beeMarioOnGround() {
    if (getPlayerMode() == PlayerMode_Bee && !mDrawStates._C && mMovementStates._1 && !mMovementStates._23 && !isStatusActive(MarioStatus_Slider)) {
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

    if (!_9F2) {
        mBeeWallWalk = 5;
        Vec3f gravity(-normal);
        _240 = gravity;
        mPosition = position;
        mMario->setTrans(position, nullptr);
        mMario->stopJump();
        mMario->stopAnimation(nullptr);
        mMario->stopWalk();
        Vec3f front(mMario->mHeadVec);
        mMario->setGravityVec(gravity);
        mMario->setHeadVec(-gravity);
        mMario->setFrontVecKeepUp(front, 1u);
        setBlendMtxTimer(2);
        _38C = 5;
        mMario->mMovementStates._38 = false;
        _214->_305 = true;
    }
}

bool Mario::beeMarioOnAir() {
    if (mBeeFlyInhibitTimer) {
        mBeeFlyInhibitTimer--;
        if (!mBeeFlyInhibitTimer) {
            mMovementStates._2F = false;
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
        if (MR::vecKillElement(mJumpVec, getAirGravityVec(), &horizontal) > -5.0f || mBeeGravityReviveTimer) {
            if (!mBeeFlyInhibitTimer) {
                f32 verticalSpeed = cutGravityElementFromJumpVec(true);
                f32 speed = mJumpVec.length();
                MR::normalizeOrZero(&mJumpVec);
                if (!MR::vecBlendSphere(mJumpVec, mFrontVec, &mJumpVec, mActor->getConst().getTable()->mBeeSpeedRotateRatio)) {
                    Mtx rotation;
                    NebulaMtxRotAxisRad(rotation, &mHeadVec, 0.1f);
                    NebulaMtxMultVecSR(rotation, &mJumpVec, &mJumpVec);
                }

                mJumpVec.setLength(speed);
                mJumpVec += getAirGravityVec() * verticalSpeed;
            }

            if (!mMovementStates._11 && mAirWalkTimer) {
                if (mBeeGravityReviveTimer < 30) {
                    mBeeGravityReviveTimer = 30;
                    mBeeUpDownSpeed = 0.0f;
                }

                mMovementStates._12 = true;
                mJumpStartPos = mPosition;
                mMovementStates._11 = true;
            }

            s16 inhibitTime = mActor->getConst().getTable()->mBeeAirWalkInhibitTime;
            s16 powerTime = mActor->getConst().getTable()->mBeeGravityPowerTime;
            Vec3f horizontalVelocity;
            MR::vecKillElement(mJumpVec, getAirGravityVec(), &horizontalVelocity);
            if (horizontalVelocity.length() < 5.0f) {
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
                    if (!mMovementStates._F && mAirWalkTimer > mActor->getConst().getTable()->mAirWalkTime) {
                        mAirWalkTimer = mActor->getConst().getTable()->mAirWalkTime;
                    }

                    mAirWalkTimer--;
                }

                if (!mAirWalkTimer) {
                    if (previousTime) {
                        playSound("ハチ体力切れ");
                    }

                    mMovementStates._11 = false;
                    stopAnimation("ハチ飛行中");
                } else {
                    if (!mMovementStates._F) {
                        if (!isAnimationRun("ハチ壁ジャンプ") && !isAnimationRun("ハチスピン空中")) {
                            changeAnimation("ハチ飛行中", "落下");
                        }

                        cancelSquatMode();
                        playSound("空中ふんばり");
                        if (mAirWalkTimer < mActor->getConst().getTable()->mAirWalkTime / 2) {
                            getAnimator()->setSpeed(1.5f);
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
                    ratio = 0.9f * (ratio * ratio);
                    f32 acceleration = 15.0f * (MR::getRandom() - ratio);
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
                        if (!getPlayer()->mHitStates._5) {
                            if (mBeeUpDownSpeed < -mActor->getConst().getTable()->mBeeUpSpeedMax) {
                                mBeeUpDownSpeed = -mActor->getConst().getTable()->mBeeUpSpeedMax;
                            }
                        } else if (mBeeUpDownSpeed < -0.5f) {
                            mBeeUpDownSpeed = -0.5f;
                        }
                    }

                    addVelocity(*getGravityVec(), mBeeUpDownSpeed);
                    if (!getPlayer()->mHitStates._5 && verticalSpeed > 0.0f) {
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
