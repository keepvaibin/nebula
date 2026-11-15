#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/CollisionCode.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioSkate.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <nebula_math.h>

void Mario::mainMove() {
    Vec3f moveDir;
    MR::setNan(moveDir);

    bool keepFront = false;

    if (mDrawStates.homingLockOn) {
        keepFront = true;
    }

    if (mMovementStates.slopeSlide) {
        mTargetWalkSpeedIndex = 0;
        mWalkSpeed = 0.0f;
        keepFront = true;

        playSound("坂滑り");
    }

    if (mCorrectionTurnTimer) {
        keepFront = true;
    }

    mMovementStates.onSkatableFloor = false;

    if (isSkatableFloor()) {
        mMovementStates.onSkatableFloor = true;
    }

    if (mActionStates.steepGravityStop) {
        tryJump();
        beforeJumping2D();

        if (isAnimationRun("その場足踏み上半身")) {
            stopAnimationUpper(nullptr);
        }

        mMoveLockTimer = 0;
        mJumpVec = mRelativeMove;
        mActionStates.steepGravityStop = false;
        return;
    }

    bool startJump = false;
    bool isRequestJump = mActor->isRequestJump();

    if (!isRequestJump && mActor->isRequestJump2P() && mJump2PDelayTimer == 0) {
        mJump2PDelayTimer = 6;
    }

    if (!isRequestJump) {
        if (mJump2PDelayTimer != 0) {
            mJump2PDelayTimer--;
            if (mJump2PDelayTimer == 0) {
                isRequestJump = true;
                mHitStates.jump2PDelayed = true;
            }
        }
    } else if (mJump2PDelayTimer != 0) {
        mHitStates.jump2PRequest = true;
        mJump2PDelayTimer = 0;
    }

    if (isRequestJump || mMovementStates.jumpBuffered) {
        if (mMovementStates.squat && calcDistToCeil(false) < 160.0f) {
            mActor->sendMsgUpperPunch(mCeilSensor);
            changeAnimation("しゃがみアッパー");
        } else if (isStatusActive(MarioStatus_Skate)) {
            mSkate->exitJump();
            closeStatus(mSkate);
        } else {
            startJump = true;
        }
    }

    if (mDashChanceTimer != 0) {
        mDashChanceTimer--;
        if (startJump && checkSquat(false)) {
            if (mStickPos.z >= 0.5f) {
                startJump = false;
                mDashChanceTimer = 0;
                mWalkSpeed = 5.0f;
                mItemDashTimer = 180;

                playSound("スペシャルダッシュ強");
                playSound("ダッシュ加速強成功");
                playSound("声物ジャンプ");
            }
        } else if (startJump && mStickPos.z >= 0.5f) {
            startJump = false;
            mDashChanceTimer = 0;
            mWalkSpeed = 2.0f;
            mItemDashTimer = 60;
            playSound("スペシャルダッシュ弱");
            playSound("声物ジャンプ");
        }
    }

    if (startJump) {
        saveLastSafetyTrans();
        tryJump();
        beforeJumping2D();

        if (isAnimationRun("その場足踏み上半身")) {
            stopAnimationUpper(nullptr);
        }

        mMoveLockTimer = 0;
        return;
    }

    if (!mMovementStates.onGround) {
        if (mDropWaitTimer != 0 && !mMovementStates.slopeSlide && MR::isNearZero(mSlopeMoveVec)) {
            mDropWaitTimer--;
        } else if (!mActor->mInDemo) {
            tryDrop();
            beforeJumping2D();
            mMoveLockTimer = 0;
            return;
        }
    } else if (mStickPos.z < mActor->getConst().getTable()->mWallBackHangStickPower) {
        mDropWaitTimer = 0;
    } else if (mDrawStates.atEdge) {
        mDropWaitTimer = 0;
    } else if (mDropWaitTimer < mActor->getConst().getTable()->mDropWaitTime) {
        mDropWaitTimer = mActor->getConst().getTable()->mDropWaitTime;
    }

    if (mMoveLockTimer != 0) {
        mMoveLockTimer--;
        mActionStates.zeroStickNextFrame = true;
        return;
    }

    checkTornado();

    getAnimator()->setWalkMode();

    if (MR::isNearZero(mStickPos.x) && MR::isNearZero(mStickPos.y)) {
        mMoveFrontVec = mPrevMoveFrontVec;

        if (MR::isNearZero(mMoveFrontVec)) {
            mMoveFrontVec = mFrontVec;
        }

        moveDir = mMoveFrontVec;

        calcShadowDir(mMoveFrontVec, &mMoveDirOnShadow);
        mWeakStickIgnoreTimer = 10;
    } else if (!(mTargetWalkSpeedIndex != 0 && mPrevWalkSpeedIndex) && (!isAnimationRun("ブレーキ") && !isAnimationRun("ターンブレーキ") && !mMovementStates.turnStickNeutral &&
                                                         !mMovementStates.tornado && mWalkSpeed < 0.05f && !mDrawStates.homingLockOn)) {
        bool stickTaken = true;
        if (mWeakStickIgnoreTimer != 0) {
            if (mStickPos.z < 0.75f) {
                stickTaken = false;
                mWeakStickIgnoreTimer--;
                mStickPos.set(0.0f);
            } else {
                mWeakStickIgnoreTimer = 0;
            }
        }

        if (stickTaken) {
            calcMoveDir(mStickPos.x, mStickPos.y, &moveDir, true);
            calcShadowDir(moveDir, &mMoveDirOnShadow);
            calcShadowDir(mFrontVec, &mFrontOnShadow);

            f32 angle = MR::pi() * 10.0f / 180.0f;
            f32 turnAngle;
            if (mMovementStates.mode2D) {
                turnAngle = MR::diffAngleAbsHorizontal(mFrontOnShadow, mMoveDirOnShadow, m2DPlaneNormal);
                angle = MR::pi() * 90.0f / 180.0f;
            } else {
                turnAngle = MR::diffAngleAbsHorizontal(mFrontOnShadow, mMoveDirOnShadow, *getGravityVec());
            }

            if (turnAngle > angle) {
                u32 turnTime = mActor->getConst().getTable()->mStandingTurnTime;
                if (mActor->isBeeWallWalk() != false) {
                    turnTime = 30;
                }

                if (!keepFront) {
                    setFrontVecKeepUp(mMoveDirOnShadow, turnTime);
                }

                if (!isEnableTurn()) {
                    return;
                }

                changeAnimation("その場足踏み");

                if (isAnimationRun("カリカリ限界")) {
                    return;
                }

                changeAnimationUpperWeak("その場足踏み上半身");
                return;
            } else if (!keepFront) {
                setFrontVecKeepUp(mMoveDirOnShadow);
            }
        } else {
            moveDir = mMoveFrontVec;
        }
    } else {
        f32 stickposX = mStickPos.x;
        f32 stickposY = mStickPos.y;

        if (!getPlayer()->mActionStates.heavySteeringStick) {
            if (MR::abs(stickposY) > mActor->getConst().getTable()->mStickMarginYstart) {
                if (MR::abs(stickposX) < mActor->getConst().getTable()->mStickMarginX) {
                    stickposX = 0.0f;
                } else if (stickposX > 0.0f) {
                    stickposX = (stickposX - mActor->getConst().getTable()->mStickMarginX) / (1.0f - mActor->getConst().getTable()->mStickMarginX);
                } else {
                    stickposX = (stickposX + mActor->getConst().getTable()->mStickMarginX) / (1.0f - mActor->getConst().getTable()->mStickMarginX);
                }
            } else if (MR::abs(stickposX) > mActor->getConst().getTable()->mStickMarginXstart) {
                if (MR::abs(stickposY) < mActor->getConst().getTable()->mStickMarginY) {
                    stickposY = 0.0f;
                } else if (stickposY > 0.0f) {
                    stickposY = (stickposY - mActor->getConst().getTable()->mStickMarginY) / (1.0f - mActor->getConst().getTable()->mStickMarginY);
                } else {
                    stickposY = (stickposY + mActor->getConst().getTable()->mStickMarginY) / (1.0f - mActor->getConst().getTable()->mStickMarginY);
                }
            }
        }

        calcMoveDir(stickposX, stickposY, &moveDir, true);

        calcShadowDir(moveDir, &mMoveDirOnShadow);
    }

    calcShadowDir(mFrontVec, &mFrontOnShadow);

    f32 dot = mFrontOnShadow.dot(mMoveDirOnShadow);
    f32 dot2 = dot;

    if (mTurnReadyTimer != 0) {
        dot2 = mTurnSlipFrontVec.dot(mMoveDirOnShadow);
    }

    if (dot >= MR::cos(mActor->getConst().getTable()->mTurnSlipAngle) && !mMovementStates.turnBrake &&
        mWalkSpeed > mActor->getConst().getTable()->mSlipSpeed && mPrevStickPower > mStickPos.z && !mDrawStates.moveDirKept) {
        recordTurnSlipAngle();
    } else if (mTurnReadyTimer != 0) {
        mTurnReadyTimer--;
    }

    if (mMovementStates.tornado || mMovementStates.onIce) {
        mTurnReadyTimer = 0;
        mMovementStates.turnStickNeutral = false;
    }

    if (mTurnReadyTimer != 0) {
        if (mStickPos.z < mActor->getConst().getTable()->mTurnSlipNeutral) {
            mMovementStates.turnStickNeutral = true;
        }

        if (mWalkSpeed < 0.1f) {
            mTurnReadyTimer = 0;
            mWalkSpeed = 0.0f;
        }

        if (isAnimationRun("ブレーキ")) {
            keepFront = true;
        }

        if (mMovementStates.turnStickNeutral) {
            keepFront = true;
        }
    } else {
        if (!mMovementStates.onIce && !mMovementStates.squat && mMovementStates.turnStickNeutral == true && !mMovementStates.tornado && !mMovementStates.slopeSlide &&
            !isStatusActive(MarioStatus_Skate) && dot2 < MR::cos(mActor->getConst().getTable()->mTurnSlipAngle) && !mMovementStates.turnBrake) {
            if (mMovementStates.onSkatableFloor) {
                keepFront = true;
            }

            mTurnFrontVec = mTurnSlipFrontVec;

            if (!keepFront) {
                setFrontVecKeepUp(-mTurnFrontVec);
            }

            if (mMovementStates.onSkatableFloor) {
                mTurnSlipTimer = mActor->getConst().getTable()->mTurnSlipTimeB;
                mMovementStates.turnBrake = true;
                changeAnimation("ターンブレーキ滑り床");
                mSlipTurnMove = mActor->getLastMove();
                stopWalk();
                mCorrectionTurnTimer = 10;
                pushTask(&Mario::taskOnSlipTurn, 1);
            } else {
                mTurnSlipTimer = mActor->getConst().getTable()->mTurnSlipTime;
                mMovementStates.turnBrake = true;
                mWalkSpeed = 0.0f;
                changeAnimation("ターンブレーキ");
                playEffect("共通ブレーキ");
            }
        }

        mMovementStates.turnStickNeutral = false;
    }

    if (mTurnSlipTimer != 0) {
        if (!mMovementStates.onSkatableFloor) {
            playSound("スリップ");
        }

        if (mTurnSlipTimer == static_cast< u16 >(mActor->getConst().getTable()->mTurnSlipTime)) {
            startPadVib(2);
        }

        mTurnSlipTimer--;

        if (!mMovementStates.onSkatableFloor) {
            mWalkSpeed = 0.0f;
        }
    }

    if (mTurnSlipTimer == 0) {
        if (mMovementStates.turnBrake) {
            Vec3f* frontVec = &mFrontVec;
            if (MR::diffAngleAbsHorizontal(getWorldPadDir(), *frontVec, *getGravityVec()) > MR::pi() * 90.0f / 180.0f) {
                stopAnimation("ターンブレーキ");
                setFrontVecKeepUp(-*frontVec);
                mMovementStates.turnBrake = false;
            }

            if (isAnimationTerminate("ターンブレーキ")) {
                stopAnimation("ターンブレーキ");
                mMovementStates.turnBrake = false;

                if (mStickPos.z < 0.1f) {
                    stopWalk();
                }
            }
        }

        if (isActiveTask(&Mario::taskOnSlipTurn)) {
            if (isAnimationRun("ターンブレーキ滑り床")) {
                setFrontVecKeepUp(-mTurnFrontVec);
                keepFront = true;

                stopAnimation("ターンブレーキ滑り床");
                mCorrectionTurnTimer = 0;
                mCorrectionAngle = 0.0f;
                mWalkSpeed = 0.0f;
                popTask(&Mario::taskOnSlipTurn);
            }
        }
    }

    f32 turnAngleSpeed = mActor->getConst().getTable()->mTurnAngleSpeed;

    if (mWalkSpeed > mActor->getConst().getTable()->mFastTurnSpeed) {
        turnAngleSpeed = mActor->getConst().getTable()->mTurnAngleSpeed2;
        if (mTargetWalkSpeedIndex < 5) {
            turnAngleSpeed = mActor->getConst().getTable()->mTurnAngleSpeedSlowWalk;
        }

        if (isStatusActive(MarioStatus_Skate)) {
            turnAngleSpeed *= 0.5f;
        }
    }

    if (mMovementStates.slopeSlide) {
        turnAngleSpeed = mActor->getConst().getTable()->mTurnAngleSpeed3;
    }

    if (mMovementStates.tornado) {
        turnAngleSpeed = mActor->getConst().getTable()->mTurnAngleSpeedTornado;
    }

    bool checkTurnBack;

    if (!mMovementStates.turnBrake) {
        Vec3f cross(mPrevMoveDirOnShadow.cross(mMoveDirOnShadow));
        Vec3f stick;
        mActor->getStickValue(&stick.x, &stick.y);
        stick.z = 0.0f;
        NebulaVecCrossProduct(&mPrevStickDir, &stick, &cross);

        checkTurnBack = true;

        if (mMovementStates.mode2D) {
            checkTurnBack = false;
        }

        do {
            if (MR::isNearZero(mStickPos.z)) {
                mActionStates.uTurn = false;
                mMovementStates.turning = false;
                break;
            }

            if (MR::isNearZero(mPrevTurnCross) && checkTurnBack) {
                bool isEqual = false;
                bool equalXY = FloatUtil::epsilonEquals(mPrevStickDir.x, stick.x, FloatUtil::epsilon()) &&
                               FloatUtil::epsilonEquals(mPrevStickDir.y, stick.y, FloatUtil::epsilon());
                if (equalXY && FloatUtil::epsilonEquals(mPrevStickDir.z, stick.z, FloatUtil::epsilon())) {
                    isEqual = true;
                }

                bool isDifferent = !isEqual;
                if (isDifferent) {
                    mMovementStates.turning = true;
                    break;
                }
            } else if (cross.dot(mPrevTurnCross) > 0.0f && checkTurnBack) {
                mMovementStates.turning = true;
                break;
            }

            if (mTurnSlipTimer != 0) {
                mActionStates.uTurn = false;
                mMovementStates.turning = false;
            }

            if (MR::isInRange(MR::diffAngleAbs(mFrontOnShadow, mMoveDirOnShadow), 0.0f, 0.707f)) {
                Vec3f cross2(mFrontOnShadow.cross((mMoveDirOnShadow)));

                if (mTurnLeft != 0) {
                    if (cross2.dot(mHeadVec) > 0.0f) {
                        mActionStates.uTurn = false;
                    }
                } else if (cross2.dot(mHeadVec) < 0.0f) {
                    mActionStates.uTurn = false;
                }
            }
        } while (false);

        mPrevStickDir = stick;

        if (mMovementStates.turning) {
            Vec3f cross3(mFrontOnShadow.cross((mMoveDirOnShadow)));

            if (cross3.dot(mTurnCross) < 0.0f) {
                if (!(cross.dot(mPrevTurnCross) > 0.0f) && !(mFrontOnShadow.dot(mMoveDirOnShadow) < 0.0f)) {
                    mWeakTurnTimer = 0;
                }
            } else if (mWeakTurnTimer < mActor->getConst().getTable()->mWeakTurnTime) {
                mWeakTurnTimer++;
            }

            mTurnCross = cross3;
            mPrevTurnCross = cross;
            mPrevMoveDirOnShadow = mMoveDirOnShadow;

            f32 weakTurnRatio = 1.0f;

            if (mWeakTurnTimer < mActor->getConst().getTable()->mWeakTurnTime && mTargetWalkSpeedIndex > 4) {
                weakTurnRatio = static_cast< f32 >(mWeakTurnTimer) / static_cast< f32 >(mActor->getConst().getTable()->mWeakTurnTime);
            }

            turnAngleSpeed *= MR::sqrt(weakTurnRatio);
        } else {
            if (mWeakTurnTimer != 0) {
                mWeakTurnTimer--;
            }

            if (mWeakTurnTimer == 0) {
                turnAngleSpeed *= 0.01f;
                mPrevTurnCross.zero();
                NebulaVecCrossProduct(&mFrontOnShadow, &mMoveDirOnShadow, &mTurnCross);
            }

            mPrevMoveDirOnShadow = mMoveDirOnShadow;
        }

        bool turnControl = true;

        if (mDrawStates.onSlipFloor) {
            if (MR::diffAngleAbsHorizontal(mSlopeMoveVec, -mFrontVec, *getGravityVec()) < MR::pi() * 45.0f / 180.0f) {
                turnAngleSpeed *= 0.1f;
            } else {
                turnAngleSpeed *= 0.3f;
            }
        }

        Vec3f vec2;

        if (turnControl) {
            if (mMovementStates.mode2D) {
                mMoveDirRetainTimer = 0;
                mDrawStates.moveDirKept = false;
                mActionStates.uTurn = false;
                vec2 = mMoveDirOnShadow;
            } else if (mActionStates.uTurn) {
                f32 angle = 0.12f;
                if (mTurnLeft == 0) {
                    angle = -angle;
                }

                Mtx mtx;
                NebulaMtxRotAxisRad(mtx, &mHeadVec, angle);
                NebulaMtxMultVecSR(mtx, &mFrontOnShadow, &vec2);
            } else if (MR::diffAngleAbs(mFrontVec, mMoveDirOnShadow) < 0.1f) {
                if (mPrevStickAngle == mStickAngle) {
                    vec2 = mMoveDirOnShadow;
                } else {
                    vec2 = mFrontVec;
                }
            } else {
                f32 diffAngleAbs = MR::diffAngleAbs(mFrontVec, mMoveDirOnShadow);

                if (diffAngleAbs > 0.0f) {
                    f32 blend = turnAngleSpeed / diffAngleAbs;
                    MR::clamp01(&blend);

                    if (mActor->isBeeWallWalk() != false && blend > 0.1f) {
                        blend = 0.1f;
                    }

                    if (getPlayer()->mActionStates.heavySteeringTurn) {
                        f32 ratio = mActor->getConst().getTable()->mStickHeavyMinRatio;
                        f32 minAngle = mActor->getConst().getTable()->mStickHeavyMinAngle;
                        f32 maxAngle = mActor->getConst().getTable()->mStickHeavyMaxAngle;
                        if (diffAngleAbs >= maxAngle) {
                            ratio = 1.0f;
                        } else if (diffAngleAbs > minAngle) {
                            ratio += (1.0f - ratio) * ((diffAngleAbs - minAngle) / (maxAngle - minAngle));
                        }

                        blend *= ratio;

                        f32 reverseRatio = 1.0f - ((MR::pi() - diffAngleAbs) / MR::pi());
                        MR::clamp01(&reverseRatio);

                        mWalkSpeed *= 1.0f - 0.1f * reverseRatio;
                    }

                    if (!MR::vecBlendSphere(mFrontVec, mMoveDirOnShadow, &vec2, blend)) {
                        mWeakTurnTimer = 0;
                        MR::vecRotAxis(mFrontOnShadow, mMoveDirOnShadow, mHeadVec, &vec2, 22.5f * MR::pi() / 180.0f);
                    }

                    if (diffAngleAbs > 2.5f && mWeakTurnTimer == mActor->getConst().getTable()->mWeakTurnTime && !mDrawStates.moveDirKept) {
                        Vec3f cross3(mFrontOnShadow.cross(vec2));
                        mTurnLeft = cross3.dot(mHeadVec) > 0.0f;
                        mActionStates.uTurn = true;
                    }
                }
            }

            mMovementStates.turning = true;

            if (!isAnimationRun("その場足踏み") && dot > 0.99f) {
                mMovementStates.turning = false;
            }

            if (mMovementStates.mode2D && mFrontVec.dot(vec2) < 0.0f && !isAnimationRun("ブレーキ")) {
                mCorrectionEaseTimer = 10;
                mCorrectionAngle = MR::pi();
            }

            if (!keepFront) {
                setFrontVecKeepUp(vec2);
            }
        }
    } else {
        if (mTurnSlipTimer == 0) {
            Vec3f blendSphere;
            if (!MR::vecBlendSphere(mFrontOnShadow, mMoveDirOnShadow, &blendSphere, turnAngleSpeed)) {
                MR::vecRotAxis(mFrontOnShadow, mMoveDirOnShadow, mHeadVec, &blendSphere, 22.5f * MR::pi() / 180.0f);
            }

            if (!keepFront) {
                setFrontVecKeepUp(blendSphere);
            }
        }

        mActionStates.uTurn = false;
        mMovementStates.turning = false;
    }

    if (mCorrectionEaseTimer != 0 && mTargetWalkSpeedIndex == 0 && isEnableTurn()) {
        changeAnimation("その場足踏み");

        if (!isAnimationRun("カリカリ限界")) {
            changeAnimationUpperWeak("その場足踏み上半身");
        }
    } else {
        if (mTargetWalkSpeedIndex != 0) {
            stopAnimation("その場足踏み");
        }

        if (isAnimationRun("その場足踏み上半身")) {
            stopAnimationUpper(nullptr);
        }
    }

    Vec3f vec2;

    if (mMovementStates.onIce) {
        Vec3f vec3(mFrontVec);
        Vec3f vec4(mFrontVec);
        if (!MR::isNearZero(mPrevVelocity)) {
            vec4 = mPrevVelocity;
        }

        MR::normalize(&vec4);

        f32 iceBlend = (1.1f - mWalkSpeed) * mActor->getConst().getTable()->mInertiaIceTurn;

        MR::clamp01(&iceBlend);

        MR::vecBlendSphere(vec4, vec3, &vec2, iceBlend);

        MR::normalize(&vec2);

        vec2.scale(mWalkSpeed * mActor->getConst().getTable()->mWalkSpeed);

    } else if (mDrawStates.homingLockOn) {
        vec2 = mMoveDirOnShadow * mActor->getConst().getTable()->mWalkSpeed * mWalkSpeed;
    } else {
        vec2 = mFrontVec * mActor->getConst().getTable()->mWalkSpeed * mWalkSpeed;
    }

    addVelocity(vec2);

    mMoveFrontVec = moveDir;

    if (mMovementStates.squat) {
        if (mTargetWalkSpeedIndex != 0) {
            mPrevMoveFrontVec = moveDir;
        }
    } else if (mTurnReadyTimer == 0) {
        mPrevMoveFrontVec = moveDir;
    }

    if (mMovementStates.frontWallGlancing && mFrontFloorHeight > 120.0f) {
        if (mVelocity.dot(*mFrontWallTriangle->getNormal(0)) < 0.0f) {
            Vec3f vec4;
            MR::vecKillElement(mPosition - mFrontWallPos, *mFrontWallTriangle->getNormal(0), &vec4);
            MR::vecKillElement(vec4, *getGravityVec(), &vec4);
            MR::vecKillElement(vec4, mFrontVec, &vec4);
            MR::normalizeOrZero(&vec4);

            if (!MR::isNearZero(vec4)) {
                mVelocity = vec4 * mVelocity.length();
            }
        }
    } else if (mMovementStates.frontWall && mVelocity.dot(*mFrontWallTriangle->getNormal(0)) < 0.0f) {
        f32 pushAngle = -mFrontVec.dot(*mFrontWallTriangle->getNormal(0));
        if (pushAngle < MR::cosDegree(mActor->getConst().getTable()->mWallPushAngleRange)) {
            MR::vecKillElement(mVelocity, *mFrontWallTriangle->getNormal(0), &mVelocity);
        }
    }

    Vec3f newVelocity(mVelocity);
    f32 speed = newVelocity.length();

    MR::normalizeOrZero(&newVelocity);

    Vec3f cross4(mGroundNorm.cross(newVelocity));
    MR::normalizeOrZero(&cross4);

    NebulaVecCrossProduct(&cross4, &mGroundNorm, &newVelocity);
    newVelocity.setLength(speed);

    mVelocity = newVelocity;
}

bool Mario::isEnableTurn() {
    if (!mMovementStates.onGround) {
        return false;
    }

    if (mMovementStates.turnBrake) {
        return false;
    }

    if (mDrawStates.homingLockOn) {
        return false;
    }

    if (mMovementStates.slopeSlide) {
        return false;
    }

    if (mMovementStates.onIce) {
        return false;
    }

    if (mDrawStates.atEdge) {
        return false;
    }

    if (mMovementStates.squat) {
        return false;
    }

    if (isStatusActive(MarioStatus_Magic)) {
        return false;
    }

    if (isAnimationRun("坂すべり上向きうつぶせ", MarioIndexedAnime_SlideUpFaceDown)) {
        return false;
    }

    if (isAnimationRun("坂すべり下向きあおむけ", MarioIndexedAnime_SlideDownFaceUp)) {
        return false;
    }

    if (isAnimationRun("坂すべり下向き終了")) {
        return false;
    }

    if (isAnimationRun("坂すべり上向き終了")) {
        return false;
    }

    if (isAnimationRun("飛び込み失敗着地")) {
        return false;
    }

    if (isAnimationRun("飛び込み失敗回転着地")) {
        return false;
    }

    if (mActor->mTaking) {
        return false;
    }

    if (mActor->isPunching()) {
        return false;
    }

    if (mActor->isItemSwinging()) {
        return false;
    }

    if (mActor->isBeeWallWalk()) {
        return false;
    }

    if (mActor->mRemoteDemo) {
        return false;
    }

    if (mActor->mInDemo) {
        return false;
    }

    if (mMovementStates.mode2D || mActionStates.circular2D) {
        return false;
    }

    return true;
}

void Mario::recordTurnSlipAngle() {
    if (!mActor->isBeeWallWalk()) {
        mTurnSlipFrontVec = mFrontVec;
        mTurnReadyTimer = mActor->getConst().getTable()->mTurnReadyTime;
    }
}

f32 Mario::decideInertia(f32 speed) {
    if (isStatusActive(MarioStatus_Skate)) {
        return decideInertiaOnIce(speed);
    }

    if (mWalkSpeed > 1.0f) {
        return mActor->getConst().getTable()->mInertiaOverSpeed;
    }

    if (mMovementStates.onIce) {
        return decideInertiaOnIce(speed);
    }

    if (mMovementStates.onSkatableFloor) {
        return decideInertiaOnSlip(speed);
    }

    f32 inertia =
        (1.0f - mWalkSpeed) * mActor->getConst().getTable()->mInertiaStandardStop + mWalkSpeed * mActor->getConst().getTable()->mInertiaStandardMax;
    inertia = (1.0f - mSpinInertiaBlend) * inertia + mSpinInertiaBlend * mActor->getConst().getTable()->mInertiaStartSpin;
    if (speed == 0.0f) {
        inertia = mActor->getConst().getTable()->mInertiaStop;
        if (mMovementStates.turnStickNeutral) {
            inertia = mActor->getConst().getTable()->mInertiaTurnSlip;
        }

        if (mMovementStates.turnBrake) {
            inertia = mActor->getConst().getTable()->mInertiaTurning;
        }

        if (mLandTimer < 30) {
            inertia = mActor->getConst().getTable()->mInertiaJumpFinish;
        }

        if (mMovementStates.squat) {
            inertia = mActor->getConst().getTable()->mInertiaSquat;
        }
    }

    if (mMovementStates.tornado) {
        if (mWalkSpeed >= speed) {
            inertia = mActor->getConst().getTable()->mInertiaTornadoBrake;
        } else {
            inertia = mActor->getConst().getTable()->mInertiaTornadoAccel;
        }
    }

    if (mReflectSlipTimer) {
        mReflectSlipTimer--;
        inertia = mActor->getConst().getTable()->mInertiaReflectSlip;
    }

    if (mWalkSpeed < 0.08f && mLandTimer > 10 && speed > 0.5f && (mMovementStates.squat || mMovementStates.spinStartWithSquat)) {
        mStartSpinTimer = mActor->getConst().getTable()->mStartSpinTime;
        mWalkSpeed = 0.08f;
    }

    if (mStartSpinTimer) {
        mStartSpinTimer--;
        inertia = mActor->getConst().getTable()->mInertiaStartSpin;
    }

    if (getFloorCode() == CollisionFloorCode_Brake && mWalkSpeed > 0.4f) {
        inertia = 0.5f * inertia;
    }

    return inertia;
}

f32 Mario::decideInertiaOnIce(f32 speed) {
    if (speed > 1.0f) {
        speed = 1.0f;
    }

    f32 inertia =
        (1.0f - speed) * mActor->getConst().getTable()->mInertiaIceStandardStop + speed * mActor->getConst().getTable()->mInertiaIceStandardMax;
    inertia = (1.0f - mSpinInertiaBlend) * inertia + mSpinInertiaBlend * mActor->getConst().getTable()->mInertiaIceStartSpin;
    if (speed == 0.0f) {
        inertia = mActor->getConst().getTable()->mInertiaIceStop;
    }

    return inertia;
}

f32 Mario::decideInertiaOnSlip(f32 speed) {
    f32 inertia = (1.0f - mWalkSpeed) * mActor->getConst().getTable()->mInertiaSlipStandardStop +
                  mWalkSpeed * mActor->getConst().getTable()->mInertiaSlipStandardMax;
    inertia = (1.0f - mSpinInertiaBlend) * inertia + mSpinInertiaBlend * mActor->getConst().getTable()->mInertiaSlipStartSpin;
    if (speed == 0.0f) {
        inertia = mActor->getConst().getTable()->mInertiaSlipStop;
    }

    return inertia;
}

void Mario::calcShadowDir(const Vec3f& rInput, Vec3f* pShadowDir) {
    Vec3f direction;
    direction = rInput;
    if (!MR::isNormalize(direction)) {
        MR::normalizeOrZero(&direction);
    }

    if (mMovementStates.mode2D || mActionStates.circular2D) {
        calcShadowDir2D(direction, pShadowDir);
        return;
    }

    Vec3f normal = -*getGravityVec();
    f32 dot = direction.dot(normal);
    direction.dot(mHeadVec);
    if (MR::abs(dot) > MR::abs(direction.dot(mGroundNorm))) {
        normal = mGroundNorm;
    }

    Vec3f side = normal.cross(direction);
    pShadowDir->cross(side, normal);
    MR::normalizeOrZero(pShadowDir);
}

bool Mario::retainMoveDir(f32 stickX, f32 stickY, Vec3f* pMoveDir) {
    f32 angle = MR::atan2(stickY, stickX);
    f32 difference = MR::diffAngleAbs(angle, mRetainStickAngle);
    if (isAnimationRun("その場足踏み")) {
        difference = 1.0f;
    }

    if (isAnimationRun("ターンブレーキ")) {
        difference = 1.0f;
    }

    if (isSwimming()) {
        difference = 1.0f;
    }

    if (mActor->isRequestSpin() && mMovementStates.jumping && mWorldPadDir.dot(mPrevVelocity) < 0.0f) {
        difference = 1.0f;
    }

    f32 limit = 0.06f;
    if (mLandTimer < 2) {
        limit = 0.1f;
    }

    if (mActionStates.stickWasNeutral) {
        if (isStickOn()) {
            mActionStates.stickWasNeutral = false;
        }

        difference = 1.0f;
    }

    bool retain = true;
    mDrawStates.retainMoveDirRan = true;
    if (mMovementStates.jumping && !mActionStates.retainInAir) {
        retain = false;
    }

    if (mMovementStates.jumping && mMovementStates.frontWall) {
        retain = false;
    }

    if (mMovementStates.jumping && !isAnimationRun(nullptr)) {
        retain = false;
    }

    if (difference < limit && isStickOn() && retain) {
        if (!mMoveDirRetainTimer || (mActionStates.retainInAir && mMovementStates.jumping)) {
            mDrawStates.moveDirKept = true;
            mMoveDirRetainTimer = 0;
        } else {
            if (mMoveDirRetainTimer) {
                mMoveDirRetainTimer--;
            }

            mRetainGroundNorm = mGroundNorm;
        }

        mRetainStickAngle = angle;
    } else {
        if (mMovementStates.jumping) {
            if (isStickOn()) {
                mActionStates.retainInAir = false;
            }
        } else {
            mActionStates.retainInAir = true;
        }

        if (!mMoveDirRetainTimer) {
            mWeakTurnTimer = 0;
        }

        mMoveDirRetainTimer = 30;
        if (!isStickOn()) {
            mActionStates.stickWasNeutral = true;
        }

        mRetainStickAngle = angle;
        mRetainGroundNorm = mGroundNorm;
    }

    return false;
}

void Mario::calcMoveDir(float stickX, float stickY, Vec3f* pMoveDir, bool isDisableMove) {
    if (mMovementStates.mode2D) {
        calcDir2D(stickX, stickY, pMoveDir);
    } else if (mActionStates.circular2D) {
        calcMoveDir2D(stickX, stickY, pMoveDir);
    } else if (mMovementStates.mode25D) {
        calcMoveDir25D(stickX, stickY, pMoveDir);
    } else if (!isDisableMove || !retainMoveDir(stickX, stickY, pMoveDir)) {
        const Vec3f& screenXDir = getCamDirX();
        const Vec3f& screenYDir = getCamDirY();
        Vec3f screenZDir = -getCamDirZ();
        Vec3f groundXDir;
        Vec3f groundYDir;
        Vec3f groundYDirOrtho;

        Vec3f marioUpDir = -*getGravityVec();
        if (!MR::isNearZero(marioUpDir)) {
            float dot = mMoveUpVec.dot(marioUpDir);
            if (dot <= 0.0f) {
                mMoveUpVec = marioUpDir;
            } else {
                MR::vecBlendSphere(mMoveUpVec, marioUpDir, &mMoveUpVec, 0.1f);
            }

            MR::normalizeOrZero(&mMoveUpVec);
        }

        f32 movementDirScreenY = screenYDir.dot(mMoveUpVec);
        f32 movementDirScreenZ = screenZDir.dot(mMoveUpVec);
        f32 absMovementDirScreenY = MR::abs(movementDirScreenY);
        f32 absMovementDirScreenZ = MR::abs(movementDirScreenZ);

        if (absMovementDirScreenZ > absMovementDirScreenY) {
            groundYDirOrtho = screenYDir;
            if (movementDirScreenZ < 0.0f) {
                groundYDirOrtho = -groundYDirOrtho;
            }

            groundXDir.cross(groundYDirOrtho, mMoveUpVec);
        } else {
            groundYDirOrtho = -screenZDir;
            if (movementDirScreenY < 0.0f) {
                groundYDirOrtho = -groundYDirOrtho;
                groundXDir.cross(screenZDir, mMoveUpVec);
            } else {
                groundXDir.cross(-screenZDir, mMoveUpVec);
            }
        }

        groundYDir.cross(screenXDir, mMoveUpVec);

        MR::normalizeOrZero(&groundXDir);
        if (MR::isNearZero(groundXDir)) {
            groundXDir = screenXDir;
        }

        MR::normalizeOrZero(&groundYDir);
        if (MR::isNearZero(groundYDir)) {
            groundYDir = groundYDirOrtho;
        }

        *pMoveDir = groundXDir * stickX - groundYDir * stickY;

        if (mActionStates.inTower) {
            Vec3f radialDir = mPosition - mTowerCenter;
            MR::vecKillElement(radialDir, mTowerAxis, &radialDir);
            MR::normalizeOrZero(&radialDir);
            MR::vecKillElement(*pMoveDir, mTowerAxis, pMoveDir);
            MR::vecKillElement(*pMoveDir, radialDir, pMoveDir);
        }
    }
}

bool Mario::checkLockOnHoming() {
    if (mStickPos.z != 0.0f) {
        return false;
    }

    if (!checkPreLvlZ()) {
        return false;
    }

    if (isSwimming()) {
        return false;
    }

    if (!mActor->mThrowTarget) {
        return false;
    }

    mMovementStates.squat = false;
    mDrawStates.homingLockOn = true;
    doLockOnHoming();
    return true;
}

void Mario::doLockOnHoming() {
    Vec3f direction;
    direction.set(mActor->mThrowTarget->mPosition - mPosition);
    MR::normalize(&direction);
    Vec3f shadowDir;
    calcShadowDir(direction, &shadowDir);
    if (MR::diffAngleAbsHorizontal(mFrontVec, direction, mHeadVec) >= 0.05235988f) {
        if (getAnimator()->isAnimationStop()) {
            changeAnimation("その場足踏み");
            if (!mCorrectionEaseTimer) {
                setFrontVecKeepUp(direction, 15u);
                mPrevMoveFrontVec = direction;
            }
        }
    }
}

void Mario::fixPositionInTower() {
    if (mActionStates.inTower) {
        Vec3f offset = mPosition - mTowerCenter;
        f32 height = MR::vecKillElement(offset, mTowerAxis, &offset);
        offset.setLength(mTowerRadius - 200.0f);
        offset += mTowerAxis * height;
        mPosition = mTowerCenter + offset;
    }
}
