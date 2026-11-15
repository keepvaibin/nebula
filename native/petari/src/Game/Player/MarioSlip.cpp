#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/MathUtil.hpp"

void Mario::clearSlope() {
    mSlipPower = 0.0f;
    mMovementStates.slopeSlide = false;
    mSlopeMoveVec.zero();
    mSlopeSideSpeed = 0.0f;
    mSlopeSlideVec.zero();
    mSlopeSideDir.zero();
    mSlopeTimer = 0;
    mWallSlope.zero();
    mSlopeSteerInhibitTimer = 0;
    mSlopeDownDir.zero();
}

bool Mario::isEnableSlopeMove() const {
    const bool isNotSlipFloor = !isSlipFloorCode(mFloorCode);
    if (isNotSlipFloor) {
        if (!mMovementStates.slopeSlide) {
            if (MR::isNearZero(mSlopeMoveVec)) {
                return false;
            }
        }
    }

    const s32 status = static_cast< s32 >(getCurrentStatus());
    switch (status) {
    case 0:
    case 2:
    case 3:
    case 4:
        return true;

    case 1:
    default:
        return false;
    }
}

void Mario::slopeMove() {
    if (mActor->mHealth == 0) {
        return;
    }

    if (!mMovementStates.onGround) {
        return;
    }

    f32 blend;
    f32 blendRate = MR::clamp(mWalkSpeed, 0.0f, 1.0f);

    Vec3f blendedDown;
    Vec3f slopeDown;
    Vec3f slopeSide;
    slopeSide.cross(mGroundNorm, getAirGravityVec());
    MR::normalizeOrZero(&slopeSide);

    if (MR::isNearZero(slopeSide)) {
        slopeDown.zero();
        mSlopeDownDir.zero();
    } else {
        slopeDown.cross(mGroundNorm, slopeSide);
        MR::normalize(&slopeDown);
        mSlopeDownDir = slopeDown;
    }

    if (MR::isSameDirection(slopeDown, getAirGravityVec()) || MR::isOppositeDirection(slopeDown, getAirGravityVec())) {
        mMovementStates.slopeSlide = false;
        mSlopeDownDir.zero();
        slopeDown.zero();
    }

    f32 slopeAngle = calcAngleD(mGroundNorm);

    if (mDrawStates.wallSlope && slopeAngle >= 30.0f) {
        if (!mMovementStates.slopeSlide) {
            mSlopeMoveVec = -mWallSlope * 2.0f;
            mMovementStates.slopeSlide = true;
            mSlopeSteerInhibitTimer = 30;
            mSlipPower = 10.0f;
        } else if (!mMovementStates.slopeSlide) {
            mSlipPower = 10.0f;
        }
    }

    if (mSlopeSteerInhibitTimer > 0xF) {
        mSlopeMoveVec += -mWallSlope * 0.5f;
    }

    if (!mDrawStates.onSlipFloor || !isSlipPolygon(mShadowPolygon)) {
        blendRate = 1.0f;
        if (mMovementStates.slopeSlide) {
            if (slopeAngle >= 15.0f) {
                mSlipPower -= 0.4f;
            } else {
                mSlipPower -= 0.8f;
            }

            if (mSlipPower < 0.0f) {
                mSlipPower = 0.0f;
                mMovementStates.slopeSlide = false;
            }

            mVelocity.zero();
        } else {
            mSlipPower = 0.0f;
        }
    } else {
        if (!mMovementStates.slopeSlide) {
            if (slopeAngle >= getActor()->getConst().getTable()->mSlipAngle) {
                Vec3f offSlope;
                if (MR::vecKillElement(mVelocity, slopeDown, &offSlope) < 0.0f) {
                    mSlopeMoveVec = mVelocity;
                } else {
                    mSlopeMoveVec.zero();
                }

                mVelocity.zero();
                mMovementStates.slopeSlide = true;
                mSlipPower = 10.0f;
                return;
            }

            Vec3f acrossSlope;
            const f32 sideSpeed = MR::vecKillElement(mVelocity, slopeDown, &acrossSlope);

            if (mWalkSpeed < 0.1f) {
                mSlipPower = 10.0f;
            }

            if (mTargetWalkSpeedIndex == 0 && !isAnimationRun("すべり着地")) {
                mSlipPower = 10.0f;
            }

            if (slopeAngle >= 25.0f && isStickOn()) {
                const Vec3f& worldPadDir = getWorldPadDir();

                Vec3f slopeDownOnScreen;
                MR::vecKillElement(slopeDown, getCamDirY(), &slopeDownOnScreen);
                if (MR::isNearZero(slopeDownOnScreen)) {
                    MR::vecKillElement(slopeDown, getCamDirZ(), &slopeDownOnScreen);
                }

                MR::normalize(&slopeDownOnScreen);
                if (slopeDownOnScreen.dot(worldPadDir) < 0.0f && mFrontVec.dot(slopeDownOnScreen) < 0.0f) {
                    mDrawStates.slopeRunUp = true;
                }
            }

            if (!mDrawStates.slopeRunUp) {
                mSlipPower += 0.3f * (slopeAngle / 90.0f);
            }

            if (mSlipPower >= 10.0f) {
                if (MR::vecKillElement(mActor->getLastMove(), slopeDown, &acrossSlope) < 0.0f) {
                    mSlopeMoveVec = mActor->getLastMove();
                } else {
                    mSlopeMoveVec.zero();
                }

                mMovementStates.slopeSlide = true;
                mSlipPower = 10.0f;
                mVelocity.zero();
                mDrawStates.slopeRunUp = false;
            } else if (!mDrawStates.slopeRunUp && sideSpeed > 0.0f) {
                const f32 t = mSlipPower / 10.0f;
                const f32 remain = 1.0f - t;

                mVelocity = acrossSlope + slopeDown * sideSpeed * remain;
            }

            blendRate *= 1.0f - (mSlipPower / 10.0f);

            if (mDrawStates.slopeRunUp) {
                mSlopeDashTimer++;

                if (mSlopeDashTimer > getActor()->getConst().getTable()->mSlopeDashAccelTime) {
                    mSlopeDashTimer = getActor()->getConst().getTable()->mSlopeDashAccelTime;
                }

                mSlopeDashSpeed = (slopeAngle / getActor()->getConst().getTable()->mSlopeDashAngleFactor) *
                       (static_cast< f32 >(mSlopeDashTimer) / static_cast< f32 >(getActor()->getConst().getTable()->mSlopeDashAccelTime));
                mSlopeDashSpeed = 1.0f + (mSlopeDashSpeed * getActor()->getConst().getTable()->mSlopeDashSpeedFactor);

                const f32 accel = 1.0f + ((mSlopeDashSpeed - 1.0f) * getActor()->getConst().getTable()->mSlopeDashAccelFactor);
                mVelocity += -mSlopeDownDir * accel;

                if (isAnimationRun("がんばり走り")) {
                    stopAnimation(nullptr);
                }
            }
        }
    }

    MR::vecKillElement(mSlopeMoveVec, *mGroundPolygon->getNormal(0), &mSlopeMoveVec);

    if (mMovementStates.slopeSlide) {
        mBrakeFirstTimer = 0;

        const f32 moveAngle = calcAngleD(mGroundNorm);
        if (mDrawStates.onSlipFloor) {
            MR::vecKillElement(mVelocity, slopeDown, &mVelocity);
            mVelocity += mSlopeMoveVec;

            mSlopeMoveVec += -slopeDown * slopeAngle * getActor()->getConst().getTable()->mSlopeAccel;
        } else {
            Vec3f slideDir;
            slideDir = mSlopeMoveVec;
            if (!MR::normalizeOrZero(&slideDir)) {
                MR::vecKillElement(mVelocity, slideDir, &mVelocity);
            }

            mVelocity += mSlopeMoveVec;
        }

        if (MR::isNearZero(mSlopeMoveVec)) {
            if (!mDrawStates.onSlipFloor) {
                mMovementStates.slopeSlide = false;
                return;
            }
        }

        if (moveAngle > 0.0f) {
            const Vec3f& airGravityVec = getAirGravityVec();
            Vec3f upSlope;
            MR::vecKillElement(-slopeDown, airGravityVec, &upSlope);

            if (MR::isNearZero(slopeDown) || (MR::isNearZero(upSlope) && !mDrawStates.onSlipFloor)) {
                mMovementStates.slopeSlide = false;
                return;
            }

            MR::normalize(&upSlope);

            const f32 speed = mSlopeMoveVec.length();
            MR::normalizeOrZero(&mSlopeMoveVec);

            const f32 ratio = MR::clamp(moveAngle / 30.0f, 0.0f, 1.0f);
            blend = ratio * getActor()->getConst().getTable()->mSlopeCurveAssist;

            MR::vecBlendSphere(mSlopeMoveVec, -slopeDown, &mSlopeMoveVec, blend);
            mSlopeMoveVec.setLength(speed);
        }

        if (MR::isNearZero(mSlopeMoveVec)) {
            mMovementStates.slopeSlide = false;
            moveSlopeSlide();
            return;
        }

        if (!(mSlopeMoveVec.dot(slopeDown) > 0.0f)) {
            if (slopeDown.dot(-*getGravityVec()) > 0.05f) {
                Vec3f frontOnGround;
                MR::vecKillElement(mFrontVec, mGroundNorm, &frontOnGround);
                MR::normalize(&frontOnGround);

                Vec3f moveDir(mSlopeMoveVec);
                MR::normalize(&moveDir);

                f32 turnDot = MR::cos(1.0471976f);
                if (isAnimationRun("坂すべり上向きうつぶせ", MarioIndexedAnime_SlideUpFaceDown)) {
                    turnDot = MR::cos(1.4959966f);
                }

                if (isAnimationRun("スケーティング") && mSlopeSlideVec.dot(slopeDown) > 0.0f) {
                    if (mSlopeSlideVec.length() > 2.0f) {
                        mSlopeMoveVec = mSlopeSlideVec;
                    }

                    mSlopeSlideVec.zero();
                }

                if (frontOnGround.dot(slopeDown) < turnDot) {
                    setFrontVecKeepUp(moveDir, 0.1f);
                    if (!isAnimationRun("坂すべり上向きうつぶせ", MarioIndexedAnime_SlideUpFaceDown)) {
                        changeAnimation("坂すべり下向きあおむけ", MarioIndexedAnime_SlideDownFaceUp);
                    }

                    if (isStickOn() && mPrevVelocity.dot(moveDir) > 0.0f &&
                        MR::diffAngleAbsHorizontal(moveDir, mFrontVec, *getGravityVec()) < 0.5235988f) {
                        setFrontVecKeepUpAngle(mPrevVelocity, getActor()->getConst().getTable()->mSlipMoveTurnAngleRad);
                    }
                } else {
                    if (!isAnimationRun("坂すべり下向きあおむけ", MarioIndexedAnime_SlideDownFaceUp)) {
                        changeAnimation("坂すべり上向きうつぶせ", MarioIndexedAnime_SlideUpFaceDown);
                    }

                    if (mSlopeTimer < 5) {
                        setFrontVecKeepUp(-moveDir, 0.5f);
                    } else {
                        setFrontVecKeepUp(-moveDir, 0.1f);
                    }

                    if (mSlopeTimer > 10 && isStickOn() && mPrevVelocity.dot(moveDir) > 0.0f) {
                        const Vec3f& gravityVec = *getGravityVec();
                        if (MR::diffAngleAbsHorizontal(-moveDir, mFrontVec, gravityVec) < 0.5235988f) {
                            setFrontVecKeepUpAngle(-mPrevVelocity, getActor()->getConst().getTable()->mSlipMoveTurnAngleRad);
                        }
                    }
                }
            } else if (mDrawStates.onSlipFloor) {
                if (!isAnimationRun("スケーティング")) {
                    mSlopeSlideVec = mSlopeMoveVec;
                    mSlopeMoveVec.zero();
                }

                changeAnimation("スケーティング");
            }
        }

        if (isStickOn() && !MR::isNearZero(slopeDown) && mSlopeSteerInhibitTimer == 0) {
            const Vec3f& worldPadDir = getWorldPadDir();

            Vec3f slopeDownPrev(slopeDown);
            MR::vecKillElement(slopeDown, getCamDirY(), &slopeDown);
            if (MR::isNearZero(slopeDown)) {
                MR::vecKillElement(slopeDownPrev, getCamDirZ(), &slopeDown);
            }

            MR::normalize(&slopeDown);
            if (slopeDown.dot(worldPadDir) > 0.15f) {
                playEffect("共通スリップ坂制動");
                mTurnSlipTimer = getActor()->getConst().getTable()->mTurnSlipTime;
            }
        }

        mPreSlopeVelocity.x *= 0.9f;
        mPreSlopeVelocity.y *= 0.9f;
        mPreSlopeVelocity.z *= 0.9f;

        Vec3f preSlopePush;
        if (MR::vecKillElement(mPreSlopeVelocity, slopeDown, &preSlopePush) >= 0.0f) {
            preSlopePush = mPreSlopeVelocity;
        }

        addVelocity(preSlopePush);
    } else {
        mVelocity += mSlopeMoveVec;

        if (isStickOn() && mSlopeSteerInhibitTimer == 0) {
            mSlopeMoveVec.scale(getActor()->getConst().getTable()->mSlopeCancelInertia);
        } else {
            mSlopeMoveVec.scale(getActor()->getConst().getTable()->mSlopeFinishInertia);
        }

        const MarioConstTable* table = getActor()->getConst().getTable();
        if (mSlopeMoveVec.length() < table->mSlopeAnimeFinishSpeed) {
            if (isAnimationRun("坂すべり上向きうつぶせ", MarioIndexedAnime_SlideUpFaceDown)) {
                changeAnimation("坂すべり上向き終了");
            }

            if (isAnimationRun("坂すべり下向きあおむけ", MarioIndexedAnime_SlideDownFaceUp)) {
                changeAnimation("坂すべり下向き終了");
            }

            if (isAnimationRun("スケーティング")) {
                stopAnimation("スケーティング");
            }

            if (isStickOn() && mSlipPower == 0.0f) {
                if (isAnimationRun("坂すべり上向き終了")) {
                    stopAnimation(nullptr);
                } else if (isAnimationRun("坂すべり下向き終了")) {
                    stopAnimation(nullptr);
                }
            }
        }
    }

    const MarioConstTable* table = getActor()->getConst().getTable();
    f32 slopeSpeedMax = table->mSlopeSpeedMax;
    if (mTurnSlipTimer != 0) {
        slopeSpeedMax = table->mSlopeSpeedMaxBraking;
    }

    const f32 slopeSpeed = mSlopeMoveVec.length();
    if (slopeSpeed > slopeSpeedMax) {
        mSlopeMoveVec.setLength((0.8f * slopeSpeed) + (0.2f * slopeSpeedMax));
    }

    if (!MR::vecBlendSphere(*getGravityVec(), -mGroundNorm, &blendedDown, blendRate)) {
        mMovementStates.slopeSlide = false;
        tryDrop();
        return;
    }

    if (mMovementStates.frontWall) {
        Vec3f offFront;
        if (MR::vecKillElement(mVelocity, mFrontVec, &offFront) > 0.0f) {
            mVelocity = offFront;
        }
    }

    moveSlopeSlide();
}

void Mario::moveSlopeSlide() {
    if (calcAngleD(mGroundNorm) < 5.8f) {
        if (mDrawStates.onSlipFloor) {
            const Vec3f* pWorldPadDir = &getWorldPadDir();
            f32 speed = mPrevVelocity.length();
            if (mSlopeSlideVec.length() < speed) {
                mSlopeSlideVec = mPrevVelocity;
            }

            mSlopeSlideVec += *pWorldPadDir * 0.5f;

            if (mSlopeSlideVec.length() > 15.0f) {
                mSlopeSlideVec.setLength(15.0f);
            }

            addVelocity(mSlopeSlideVec);
        }
    } else if (isAnimationRun("スケーティング") || isAnimationRun("坂すべり上向きうつぶせ", MarioIndexedAnime_SlideUpFaceDown) || isAnimationRun("坂すべり下向きあおむけ", MarioIndexedAnime_SlideDownFaceUp)) {
        Vec3f sideDir;
        sideDir.cross(getAirGravityVec(), mGroundNorm);
        MR::normalizeOrZero(&sideDir);

        const Vec3f* pWorldPadDir = &getWorldPadDir();
        if (MR::isNearZero(mSlopeSideSpeed)) {
            mSlopeSideDir = sideDir;
        }

        if (isStickOn() && mSlopeSteerInhibitTimer == 0) {
            f32 dot = mSlopeSideDir.dot(*pWorldPadDir);
            if (MR::abs(dot) > 0.2f) {
                mSlopeSideSpeed = (mSlopeSideSpeed * mActor->getConst().getTable()->mSlopeSideMoveInertia) +
                       (dot * (1.0f - mActor->getConst().getTable()->mSlopeSideMoveInertia));
            } else {
                MarioConstTable* pConstTable = mActor->getConst().getTable();
                mSlopeSideSpeed = mSlopeSideSpeed * pConstTable->mSlopeSideStopInertia;
            }
        } else {
            MarioConstTable* pConstTable = mActor->getConst().getTable();
            mSlopeSideSpeed = mSlopeSideSpeed * pConstTable->mSlopeSideStopInertia;
        }

        Vec3f slopeFront;
        slopeFront.cross(mSlopeSideDir, mGroundNorm);
        mSlopeSideDir.cross(mGroundNorm, slopeFront);

        MarioConstTable* pConstTable = mActor->getConst().getTable();
        addVelocity(mSlopeSideDir, mSlopeSideSpeed * pConstTable->mSlopeSideMoveSpeed);
    }
}

bool Mario::taskOnSlipTurn(u32) {
    if (!isAnimationRun("ターンブレーキ滑り床")) {
        setFrontVecKeepUp(-mTurnFrontVec);
        mCorrectionTurnTimer = 0;
        mCorrectionAngle = 0.0f;
        return false;
    }

    mSlipTurnMove.scale(mActor->mConst->getTable()->mInertiaSlipTurn);
    addVelocity(mSlipTurnMove);
    return true;
}
