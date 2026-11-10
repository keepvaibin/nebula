#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f1, "slopeMove.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f2, "slopeMove.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f3, "slopeMove.f3", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f4, "slopeMove.f4", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f5, "slopeMove.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f6, "slopeMove.f6", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f7, "slopeMove.f7", 0.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f8, "slopeMove.f8", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f9, "slopeMove.f9", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f10, "slopeMove.f10", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f11, "slopeMove.f11", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f12, "slopeMove.f12", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f13, "slopeMove.f13", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f14, "slopeMove.f14", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f15, "slopeMove.f15", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f16, "slopeMove.f16", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f17, "slopeMove.f17", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f18, "slopeMove.f18", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f19, "slopeMove.f19", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f20, "slopeMove.f20", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f21, "slopeMove.f21", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f22, "slopeMove.f22", 1.0471976f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f23, "slopeMove.f23", 1.4959966f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f24, "slopeMove.f24", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f25, "slopeMove.f25", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f26, "slopeMove.f26", 0.5235988f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f27, "slopeMove.f27", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f28, "slopeMove.f28", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f29, "slopeMove.f29", 0.5235988f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f30, "slopeMove.f30", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f31, "slopeMove.f31", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f32, "slopeMove.f32", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f33, "slopeMove.f33", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f34, "slopeMove.f34", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_slopeMove_f35, "slopeMove.f35", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveSlopeSlide_f1, "moveSlopeSlide.f1", 5.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveSlopeSlide_f2, "moveSlopeSlide.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveSlopeSlide_f3, "moveSlopeSlide.f3", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveSlopeSlide_f4, "moveSlopeSlide.f4", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_moveSlopeSlide_f5, "moveSlopeSlide.f5", 0.2f);

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

    if (mDrawStates.wallSlope && slopeAngle >= cNum_slopeMove_f1) {
        if (!mMovementStates.slopeSlide) {
            mSlopeMoveVec = -mWallSlope * cNum_slopeMove_f2;
            mMovementStates.slopeSlide = true;
            mSlopeSteerInhibitTimer = 30;
            mSlipPower = cNum_slopeMove_f3;
        } else if (!mMovementStates.slopeSlide) {
            mSlipPower = cNum_slopeMove_f4;
        }
    }

    if (mSlopeSteerInhibitTimer > 0xF) {
        mSlopeMoveVec += -mWallSlope * cNum_slopeMove_f5;
    }

    if (!mDrawStates.onSlipFloor || !isSlipPolygon(mShadowPolygon)) {
        blendRate = 1.0f;
        if (mMovementStates.slopeSlide) {
            if (slopeAngle >= cNum_slopeMove_f6) {
                mSlipPower -= cNum_slopeMove_f7;
            } else {
                mSlipPower -= cNum_slopeMove_f8;
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
                mSlipPower = cNum_slopeMove_f9;
                return;
            }

            Vec3f acrossSlope;
            const f32 sideSpeed = MR::vecKillElement(mVelocity, slopeDown, &acrossSlope);

            if (mWalkSpeed < cNum_slopeMove_f10) {
                mSlipPower = cNum_slopeMove_f11;
            }

            if (mTargetWalkSpeedIndex == 0 && !isAnimationRun("すべり着地")) {
                mSlipPower = cNum_slopeMove_f12;
            }

            if (slopeAngle >= cNum_slopeMove_f13 && isStickOn()) {
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
                mSlipPower += cNum_slopeMove_f14 * (slopeAngle / cNum_slopeMove_f15);
            }

            if (mSlipPower >= cNum_slopeMove_f16) {
                if (MR::vecKillElement(mActor->getLastMove(), slopeDown, &acrossSlope) < 0.0f) {
                    mSlopeMoveVec = mActor->getLastMove();
                } else {
                    mSlopeMoveVec.zero();
                }

                mMovementStates.slopeSlide = true;
                mSlipPower = cNum_slopeMove_f17;
                mVelocity.zero();
                mDrawStates.slopeRunUp = false;
            } else if (!mDrawStates.slopeRunUp && sideSpeed > 0.0f) {
                const f32 t = mSlipPower / cNum_slopeMove_f18;
                const f32 remain = 1.0f - t;

                mVelocity = acrossSlope + slopeDown * sideSpeed * remain;
            }

            blendRate *= 1.0f - (mSlipPower / cNum_slopeMove_f19);

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

            const f32 ratio = MR::clamp(moveAngle / cNum_slopeMove_f20, 0.0f, 1.0f);
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
            if (slopeDown.dot(-*getGravityVec()) > cNum_slopeMove_f21) {
                Vec3f frontOnGround;
                MR::vecKillElement(mFrontVec, mGroundNorm, &frontOnGround);
                MR::normalize(&frontOnGround);

                Vec3f moveDir(mSlopeMoveVec);
                MR::normalize(&moveDir);

                f32 turnDot = MR::cos(cNum_slopeMove_f22);
                if (isAnimationRun("坂すべり上向きうつぶせ", MarioIndexedAnime_SlideUpFaceDown)) {
                    turnDot = MR::cos(cNum_slopeMove_f23);
                }

                if (isAnimationRun("スケーティング") && mSlopeSlideVec.dot(slopeDown) > 0.0f) {
                    if (mSlopeSlideVec.length() > cNum_slopeMove_f24) {
                        mSlopeMoveVec = mSlopeSlideVec;
                    }

                    mSlopeSlideVec.zero();
                }

                if (frontOnGround.dot(slopeDown) < turnDot) {
                    setFrontVecKeepUp(moveDir, cNum_slopeMove_f25);
                    if (!isAnimationRun("坂すべり上向きうつぶせ", MarioIndexedAnime_SlideUpFaceDown)) {
                        changeAnimation("坂すべり下向きあおむけ", MarioIndexedAnime_SlideDownFaceUp);
                    }

                    if (isStickOn() && mPrevVelocity.dot(moveDir) > 0.0f &&
                        MR::diffAngleAbsHorizontal(moveDir, mFrontVec, *getGravityVec()) < cNum_slopeMove_f26) {
                        setFrontVecKeepUpAngle(mPrevVelocity, getActor()->getConst().getTable()->mSlipMoveTurnAngleRad);
                    }
                } else {
                    if (!isAnimationRun("坂すべり下向きあおむけ", MarioIndexedAnime_SlideDownFaceUp)) {
                        changeAnimation("坂すべり上向きうつぶせ", MarioIndexedAnime_SlideUpFaceDown);
                    }

                    if (mSlopeTimer < 5) {
                        setFrontVecKeepUp(-moveDir, cNum_slopeMove_f27);
                    } else {
                        setFrontVecKeepUp(-moveDir, cNum_slopeMove_f28);
                    }

                    if (mSlopeTimer > 10 && isStickOn() && mPrevVelocity.dot(moveDir) > 0.0f) {
                        const Vec3f& gravityVec = *getGravityVec();
                        if (MR::diffAngleAbsHorizontal(-moveDir, mFrontVec, gravityVec) < cNum_slopeMove_f29) {
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
            if (slopeDown.dot(worldPadDir) > cNum_slopeMove_f30) {
                playEffect("共通スリップ坂制動");
                mTurnSlipTimer = getActor()->getConst().getTable()->mTurnSlipTime;
            }
        }

        mPreSlopeVelocity.x *= cNum_slopeMove_f31;
        mPreSlopeVelocity.y *= cNum_slopeMove_f32;
        mPreSlopeVelocity.z *= cNum_slopeMove_f33;

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
        mSlopeMoveVec.setLength((cNum_slopeMove_f34 * slopeSpeed) + (cNum_slopeMove_f35 * slopeSpeedMax));
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
    if (calcAngleD(mGroundNorm) < cNum_moveSlopeSlide_f1) {
        if (mDrawStates.onSlipFloor) {
            const Vec3f* pWorldPadDir = &getWorldPadDir();
            f32 speed = mPrevVelocity.length();
            if (mSlopeSlideVec.length() < speed) {
                mSlopeSlideVec = mPrevVelocity;
            }

            mSlopeSlideVec += *pWorldPadDir * cNum_moveSlopeSlide_f2;

            if (mSlopeSlideVec.length() > cNum_moveSlopeSlide_f3) {
                mSlopeSlideVec.setLength(cNum_moveSlopeSlide_f4);
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
            if (MR::abs(dot) > cNum_moveSlopeSlide_f5) {
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
