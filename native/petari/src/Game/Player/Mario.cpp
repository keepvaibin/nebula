#include "Game/Player/Mario.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Animation/XanimeCore.hpp"
#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/MarioAbyssDamage.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioBlown.hpp"
#include "Game/Player/MarioBump.hpp"
#include "Game/Player/MarioClimb.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioCrush.hpp"
#include "Game/Player/MarioDamage.hpp"
#include "Game/Player/MarioDarkDamage.hpp"
#include "Game/Player/MarioFaint.hpp"
#include "Game/Player/MarioFireDamage.hpp"
#include "Game/Player/MarioFireDance.hpp"
#include "Game/Player/MarioFireRun.hpp"
#include "Game/Player/MarioFlip.hpp"
#include "Game/Player/MarioFlow.hpp"
#include "Game/Player/MarioFoo.hpp"
#include "Game/Player/MarioFpView.hpp"
#include "Game/Player/MarioFreeze.hpp"
#include "Game/Player/MarioFrontStep.hpp"
#include "Game/Player/MarioHang.hpp"
#include "Game/Player/MarioMagic.hpp"
#include "Game/Player/MarioMapCode.hpp"
#include "Game/Player/MarioMove.hpp"
#include "Game/Player/MarioParalyze.hpp"
#include "Game/Player/MarioRabbit.hpp"
#include "Game/Player/MarioRecovery.hpp"
#include "Game/Player/MarioSideStep.hpp"
#include "Game/Player/MarioSkate.hpp"
#include "Game/Player/MarioSlider.hpp"
#include "Game/Player/MarioStep.hpp"
#include "Game/Player/MarioStick.hpp"
#include "Game/Player/MarioStun.hpp"
#include "Game/Player/MarioSukekiyo.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Player/MarioTalk.hpp"
#include "Game/Player/MarioTeresa.hpp"
#include "Game/Player/MarioWait.hpp"
#include "Game/Player/MarioWall.hpp"
#include "Game/Player/MarioWarp.hpp"
#include "Game/Util.hpp"

Mario::Mario(MarioActor* pActor) : MarioModule(pActor) {
    initMember();
    mVelocity.zero();
    mLookOfs.zero();
    mLookOfsBody.zero();
    mLookOfsSink.zero();
    mFrontVec.set(1.0f, 0.0f, 0.0f);
    mTurnFrontVec = mFrontVec;
    mPrevFrontVec = mFrontVec;
    mFrontOnShadow.set(1.0f, 0.0f, 0.0f);
    mMoveDirOnShadow.set(1.0f, 0.0f, 0.0f);
    mTurnSlipFrontVec.set(1.0f, 0.0f, 0.0f);
    mRetainStickAngle = 0.0f;
    mSideVec.set(0.0f, 0.0f, 1.0f);
    mGravityFixSideVec = mSideVec;
    mAirGravityVec.set(0.0f, -1.0f, 0.0f);
    mHeadVec = -mAirGravityVec;
    mPostureUpVec = mHeadVec;
    mPrevMtxUp = mHeadVec;
    mPrevMtxFront = mFrontVec;
    mPrevMtxSide = mSideVec;
    mCameraTurnHold = false;
    mTargetWalkSpeedIndex = 0;
    mWalkSpeed = 0.0f;
    mWalkingResist = 0.0f;
    mJumpVec.zero();
    mJumpFrontVec.zero();
    mJumpGravityVec.zero();
    mFireShotCount = 0;
    mHipDropJumpVec.zero();
    mMoveFrontVec.zero();
    mPrevMoveFrontVec.zero();
    mVerticalSpeed = 0.0f;
    mJumpKind = 0;
    mAirTimer = 0;
    mJumpTimer = 0;
    mDropWaitTimer = 0;
    mLandTimer = 0;
    mBackAirWalkTimer = 0;
    mBackJumpTimer = 0;
    mTurnSlipTimer = 0;
    mWeakTurnTimer = 0;
    mZeroGravityTimer = 0;
    mItemDashTimer = 0;
    mDashChanceTimer = 0;
    mAirControlInhibitTimer = 0;
    mTurnReadyTimer = 0;
    mSlowStartTimer = 0;
    mBeeGravityTimer = 0;
    mTornadoBoostTimer = 0;
    mTeresaWallThroughTimer = 0;
    mBeeFlyInhibitTimer = 0;
    mSlopeTimer = 0;
    mSlopeSteerInhibitTimer = 0;
    mEdgePushTimer = 0;
    mJump2PDelayTimer = 0;
    mKarikariTimer = 0x78;
    mPrevStickAngle = 0.0f;
    mStickAngle = 0.0f;
    mTornadoTimer = 0;
    mYAngleOffset = 0.0f;
    mReflectSlipTimer = 0;
    mStartSpinTimer = 0;
    mSlopeDashTimer = 0;
    mAirWalkTimer = 0;
    mPrevAnimFrame = 0.0f;
    mIceAnimFoot = 0;
    mSinkTimer = 0;
    mPoisonTimer = 0;
    mBrakeFirstTimer = 0;
    mLedgeFallTimer = 0;
    mMorphResetTimer = 0;
    mSlipGraceTimer = 0;
    mDamageAfterTimer = 0;
    mMoveLockTimer = 0;
    mSlipInhibitTimer = 0;
    mSpinInertiaBlend = 0.0f;
    mHandScaleX = 1.0f;
    mBeeGravityReviveTimer = 0;
    mBeeUpDownSpeed = 0.0f;
    mRippleHeight = 0.0f;
    mRippleRotate.set(0.0f, 1.0f, 0.0f);
    mGroundNorm.set(0.0f, 1.0f, 0.0f);
    mGroundDown = -mGroundNorm;
    mWallNorm = mGroundNorm;
    mWallGravityVec = -mGroundNorm;
    mMoveUpVec.set(0.0f, 1.0f, 0.0f);
    mFixSideVec.set(1.0f, 0.0f, 0.0f);
    mCorrectionAngle = 0.0f;
    mCorrectionEaseTimer = 0;
    mCorrectionTurnTimer = 0;
    mWeakStickIgnoreTimer = 0;
    mMoveDirRetainTimer = 0;
    mJumpFrame = 0;
    mRetainGroundNorm = mGroundNorm;
    mTornadoTilt = 0.0f;
    mTornadoTiltUp = mHeadVec;
    clearSlope();
    mSlopeSlideVec.zero();
    mWindVec.zero();
    mReactionVec.zero();
    mMovementStates = MovementStates();
    mActionStates = ActionStates();
    mDrawStates = DrawStates();
    mHitStates = HitStates();
    mPrevMovementStates = MovementStates();
    mPrevActionStates = ActionStates();

    mShadowFilter = MR::createTriangleFilterDelegator(this, &Mario::isIgnoreTriangle);

    mShadowPolygon = new Triangle();
    mSlopeFloorPolygon = new Triangle();
    mGroundPolygon = new Triangle();

    mCeilPolygon = new Triangle();
    mFrontWallTriangle = new Triangle();
    mBackWallTriangle = new Triangle();
    mSideWallTriangle = new Triangle();
    mStepPolygon = new Triangle();
    mBumpPolygon = new Triangle();

    mLastGroundPolygon = new Triangle();
    mTmpPolygon = new Triangle();

    mSafetyPolygon = new Triangle();

    mLastSafetyPolygon = new Triangle();

    mFrontFloorPolygon = new Triangle();
    mPressFloorPolygon = new Triangle();
    mPressCeilPolygon = new Triangle();

    mFrontWallPos.zero();
    mBackWallPos.zero();

    mSafetyPos.zero();
    mRelativePos.zero();

    mRelativePolygon = new Triangle();

    mJumpEnforcePolygon = new Triangle();

    mRelativeWallPolygons[0] = new Triangle();
    mRelativeWallPolygons[1] = new Triangle();

    mArrivedWarpCube = 0;

    mFrontFloorPos.zero();
    mJumpStartPos.zero();

    mBinderHitPos.zero();
    mBinderHitNorm.zero();

    mPushVec.zero();
    mPushVelocity.zero();

    mRelativeMove.zero();

    mPreSlopeVelocity.zero();

    mAreaMoveVelocity.zero();
    mRailMoveVelocity.zero();

    mFrontWallDist = 0.0f;
    mFrontFloorHeight = 0.0f;

    mCeilDist = 200.0f;

    mCeilSensor = nullptr;

    mJumpEnforceSensor = nullptr;
    mRelativeSensor = nullptr;

    mTornadoCenteringSensor = nullptr;
    mHipDropSlideSensor = 0;

    mOnimasuSensor = 0;

    mWorldPadDir.zero();

    mPrevStickPower = 0.0f;
    mStickPos.z = 0.0f;
    mStickPos.y = 0.0f;
    mStickPos.x = 0.0f;

    mTurnCross.zero();

    mPrevTurnCross.zero();
    mPrevMoveDirOnShadow.zero();
    mPrevStickDir.zero();

    clear2DStick();

    m25DMode = 0;
    m25DSideVec.zero();
    m25DDownVec.zero();

    mSide2D.set(1.0f, 0.0f, 0.0f);
    mUp2D.set(0.0f, 1.0f, 0.0f);
    mNormal2D.set(0.0f, 0.0f, 1.0f);
    m2DPlaneNormal = mNormal2D;

    initSound();

    mCurrentState = 0;

    mFlow = new MarioFlow(mActor);
    mWall = new MarioWall(mActor);
    mDamage = new MarioDamage(mActor);
    mFaint = new MarioFaint(mActor);
    mBlown = new MarioBlown(mActor);
    mHang = new MarioHang(mActor);
    mSwim = new MarioSwim(mActor);
    mSlider = new MarioSlider(mActor);
    mFireDamage = new MarioFireDamage(mActor);
    mFireRun = new MarioFireRun(mActor);
    mFireDance = new MarioFireDance(mActor);
    mAbyssDamage = new MarioAbyssDamage(mActor);
    mDarkDamage = new MarioDarkDamage(mActor);
    mStep = new MarioStep(mActor);
    mBump = new MarioBump(mActor);
    mParalyze = new MarioParalyze(mActor);
    mStun = new MarioStun(mActor);
    mCrush = new MarioCrush(mActor);
    mFreeze = new MarioFreeze(mActor);
    mMagic = new MarioMagic(mActor);
    mFpView = new MarioFpView(mActor);
    mRecovery = new MarioRecovery(mActor);
    mFlip = new MarioFlip(mActor);
    mSideStep = new MarioSideStep(mActor);
    mFrontStep = new MarioFrontStep(mActor);
    mStick = new MarioStick(mActor);
    mRabbit = new MarioRabbit(mActor);
    mSukekiyo = new MarioSukekiyo(mActor);
    mBury = new MarioBury(mActor);
    mWait = new MarioWait(mActor);
    mClimb = new MarioClimb(mActor);
    mSkate = new MarioSkate(mActor);
    mFoo = new MarioFoo(mActor);
    mWarp = new MarioWarp(mActor);
    mTeresa = new MarioTeresa(mActor);
    mTalk = new MarioTalk(mActor);
    mMove = new MarioMove(mActor);

    initTask();
    mFloorCodeReader = new FloorCode();
    mFloorCode = 0;
    mPolygonFloorCode = 0;
    for (int i = 0; i < ARRAY_SIZE(mWallFloorCodes); i++) {
        mWallFloorCodes[i] = mFloorCode;
    }

    mNotSafetyTimer = 0;
    mGroundCheckOwner = 0;
    mFpViewChangeFailed = false;
}

void Mario::updateAndClearStrideParameter() {
    mPrevFrontVec = mFrontVec;
    mPrevVelocity = mVelocity;
    mPosition = mActor->mPosition;
    mVelocityAfter.zero();
    mVelocity.zero();
    mPrevMovementStates = mMovementStates;
    mPrevActionStates = mActionStates;
    mPrevDrawStates = mDrawStates;
    mDrawStates = DrawStates();
    mHitStates = HitStates();
    if (mMovementStates.onGround) {
        mWall->mCancel = 0;
    }

    if (mActionStates.gravityFlipPending) {
        mHitStates.gravityFlip = 1;
    }

    if (mActionStates.gravityFrontTurnPending) {
        mHitStates.gravityFrontTurn = 1;
    }

    if (mActionStates.gravityChangeSeen) {
        mHitStates.gravityChangeSeen = 1;
    }

    mActionStates.gravityFlipPending = 0;
    mActionStates.gravityFrontTurnPending = 0;
}

void Mario::checkKeyLock() {
    if (!mMovementStates.jumping) {
        mMovementStates.inputLocked = false;
    }

    if (mActor->mFrameCount < 0x4b) {
        mMovementStates.inputLocked = true;
    }

    if (mActor->isEnableNerveChange() == false) {
        mMovementStates.inputLocked = true;
    }

    if (isSwimming()) {
        mMovementStates.inputLocked = false;
    }

    if (MR::isHeldCamera(PLAYER_1)) {
        mDrawStates.stickLocked = true;
    }
}

void Mario::updateTimers() {
    if (mMovementStates.onGround) {
        mLandTimer++;
    }

    if (mLedgeFallTimer != 0) {
        mLedgeFallTimer--;
    }

    if (mItemDashTimer != 0) {
        mItemDashTimer--;
    }

    if (mHang->mHangTimer != 0) {
        mHang->mHangTimer--;
    }
}

void Mario::updateMorphResetTimer() {
    u16 timer = mMorphResetTimer;
    if (timer == 0) {
        return;
    }

    timer--;
    mMorphResetTimer = timer;
    if (timer != 0) {
        return;
    }

    mActor->setPlayerMode(PlayerMode_Normal, true);
    return;
}

void Mario::doExtraServices() {
    Vec3f jumpOffGravity;
    Vec3f fallOffGravity;
    if (MR::diffAngleAbs(mPrevFrontVec, mFrontVec) < mActor->mConst->getTable()->mFrontAngleFixMargin) {
        setFrontVec(mPrevFrontVec);
    }

    bool notBee = !isPlayerModeBee();
    if (notBee) {
        bool notTeresa = !isPlayerModeTeresa();
        if (notTeresa && mMovementStates.jumping && mAirTimer > 600 && !MR::isNearZero(*getGravityVec())) {
            Vec3f vec(*getGravityVec() * MR::vecKillElement(mJumpVec, *getGravityVec(), &jumpOffGravity));
            mJumpVec = vec;
        }
    }

    bool notFoo = !isPlayerModeFoo();
    if (notFoo) {
        bool notTeresa = !isPlayerModeTeresa();
        if (notTeresa && mMovementStates.jumping && mAirTimer > 300) {
            Vec3f fromSafety(mPosition - *getLastSafetyTrans(nullptr));

            if (MR::vecKillElement(fromSafety, mAirGravityVec, &fallOffGravity) > 28000.0f) {
                if (mVerticalSpeed > 2499.0f || mActor->isInZeroGravitySpot()) {
                    mActor->forceKill(0);
                }
            }
        }
    }

    if (getCurrentStatus() != 0 || mActor->mRemoteDemo != 0 || mActor->mInDemo) {
        mActionStates.uTurn = 0;
    }
}

bool Mario::isEnableCheckGround() {
    return !isStatusActive(MarioStatus_Swim);
}

void Mario::setGroundNorm(const Vec3f& rVec) {
    if (!(MR::diffAngleAbs(-rVec, mAirGravityVec) > 1.4959966f)) {
        mGroundNorm = rVec;
        mGroundDown = -rVec;
    }
}

void Mario::checkForceGrounding() {
    if ((mMovementStates.mode2D || mActionStates.circular2D) && mMovementStates.onGround && MR::diffAngleAbs(mGroundNorm, *mShadowPolygon->getNormal(0)) < MR::toRadian(45.0f)) {
        mDrawStates.snappedToGround = true;
    }

    if (mMovementStates.frontWall || mMovementStates.backWall || mMovementStates.sideWall) {
        return;
    }

    // does not match if this if statement is an early return
    if (!mMovementStates.frontLedge && !mMovementStates.slopeSlide && mMovementStates.onGround) {
        if (!mDrawStates.snappedToGround) {
            Vec3f vec;

            f32 dot = getGravityVec()->dot(-mGroundNorm);

            if (dot > 0.99f) {
                vec = *getGravityVec();
            } else {
                vec = -mGroundNorm;
            }

            f32 dot2 = MR::vecKillElement(mVelocity, vec, &mVelocity);

            dot *= (mShadowPos - mPosition).dot(vec);

            if (MR::isNearZero(dot)) {
                return;
            }

            if (!(MR::abs(dot) < 30.0f)) {
                return;
            }

            if (mMovementStates.frontWall) {
                dot2 = dot;
            }

            if (dot2 > dot) {
                dot2 = dot;
            }

            mVelocity += vec * dot2;

            return;
        }

        Vec3f vec2;
        f32 dot3 = MR::vecKillElement(mVelocity, *getGravityVec(), &vec2);
        f32 offGravitySpeed = vec2.length();

        if (mVerticalSpeed && dot3 > mVerticalSpeed) {
            dot3 = mVerticalSpeed;
        }

        if (!mDrawStates.slopeRunUp) {
            offGravitySpeed = MR::abs(dot3) > offGravitySpeed ? 0.0f : MR::sqrt(offGravitySpeed * offGravitySpeed - dot3 * dot3);
        }

        MR::normalizeOrZero(&vec2);

        mVelocity = (vec2 * offGravitySpeed) + *getGravityVec() * dot3;
    }
}

void Mario::fixHeadFrontVecByGravity() {
    Vec3f startGroundNorm(mGroundNorm);

    if (MR::diffAngleAbs(mActor->mCamDirZ, mActor->mPrevCamDirZ) >= MR::toRadian(45.0f) && isStickOn()) {
        mCameraTurnHold = true;
        mDrawStates.moveDirKept = true;

        mActionStates.retainInAir = true;
        mMoveDirRetainTimer = 0;
    }

    if (mTurnSlipTimer != 0 || mTurnReadyTimer != 0) {
        mDrawStates.moveDirKept = false;
    }

    if (!mCameraTurnHold && mActor->mGravitySameAhead) {
        mDrawStates.moveDirKept = false;
    }

    if (isStatusActive(MarioStatus_Bump) || isStatusActive(MarioStatus_Climb)) {
        mCameraTurnHold = true;
    }

    Vec3f headVecCopy(mHeadVec);

    Vec3f minusGravDir(-*getGravityVec());
    MR::normalize(&minusGravDir);

    f32 rotateHeadVecSpeedByGravity;

    if (minusGravDir.dot(mHeadVec) < 0.0f) {
        if (mActor->mGravityJumpTimer != 0) {
            f32 lerpFactor = (static_cast< f32 >(mActor->mGravityJumpTimer) / 15.0f);
            rotateHeadVecSpeedByGravity = (lerpFactor * mActor->getConst().getTable()->mRotateHeadVecSpeedByGravityM) +
                                          ((1.0f - lerpFactor) * mActor->getConst().getTable()->mRotateHeadVecSpeedByGravityL);
        } else {
            rotateHeadVecSpeedByGravity = mActor->getConst().getTable()->mRotateHeadVecSpeedByGravityL;
        }
    } else if (minusGravDir.dot(mHeadVec) < 0.99f) {
        rotateHeadVecSpeedByGravity = mActor->getConst().getTable()->mRotateHeadVecSpeedByGravityM;
    } else {
        rotateHeadVecSpeedByGravity = mActor->getConst().getTable()->mRotateHeadVecSpeedByGravityS;
    }

    if (mCameraTurnHold && mTargetWalkSpeedIndex != 0 && (mMovementStates.onGround || !mMovementStates.jumping)) {
        if (mDrawStates.moveDirKept) {
            Vec3f vec(mAirGravityVec);
            if (MR::normalizeOrZero(&vec)) {
                return;
            }

            vec = -vec;

            Vec3f cross;
            cross.cross(mGravityFixSideVec, vec);

            if (MR::normalizeOrZero(&cross)) {
                return;
            }

            Vec3f cross2;
            cross2.cross(vec, cross);

            if (MR::normalizeOrZero(&cross2)) {
                return;
            }

            mGravityFixSideVec = cross2;
            mSideVec = mGravityFixSideVec;

            MR::normalize(&mSideVec);

        }

        Vec3f newHeadVec;

        if (!MR::vecBlendSphere(mHeadVec, minusGravDir, &newHeadVec, rotateHeadVecSpeedByGravity)) {
            MR::vecRotAxis(mHeadVec, minusGravDir, mSideVec, &newHeadVec, rotateHeadVecSpeedByGravity);
        }

        mHeadVec = newHeadVec;
        MR::normalize(&mHeadVec);

        fixFrontVecFromUpSide();

        mRetainGroundNorm = startGroundNorm;

        mGravityFixSideVec = mSideVec;
        return;
    }

    bool alongFront = true;

    Vec3f headSide;
    headSide.cross(mHeadVec, minusGravDir);

    if (MR::isNearZero(headSide)) {
        if (MR::abs(minusGravDir.dot(mSideVec)) < MR::abs(minusGravDir.dot(mFrontVec))) {
            alongFront = false;
        }
    } else if (MR::abs(headSide.dot(mFrontVec)) < MR::abs(headSide.dot(mSideVec))) {
        alongFront = false;
    }

    if (alongFront) {
        Vec3f newHeadVec;

        if (!MR::vecBlendSphere(mHeadVec, minusGravDir, &newHeadVec, rotateHeadVecSpeedByGravity)) {
            MR::vecRotAxis(mHeadVec, minusGravDir, mFrontVec, &newHeadVec, rotateHeadVecSpeedByGravity);
        }

        mHeadVec = newHeadVec;
        MR::normalize(&mHeadVec);

        setFrontVecKeepUp(mFrontVec);

    } else {
        Vec3f newHeadVec;

        if (!MR::vecBlendSphere(mHeadVec, minusGravDir, &newHeadVec, rotateHeadVecSpeedByGravity)) {
            MR::vecRotAxis(mHeadVec, minusGravDir, mSideVec, &newHeadVec, rotateHeadVecSpeedByGravity);
        }

        mHeadVec = newHeadVec;
        MR::normalize(&mHeadVec);

        fixFrontVecFromUpSide();
    }

    if (MR::diffAngleAbs(mPrevFrontVec, mFrontVec) < mActor->getConst().getTable()->mFrontAngleFixMargin) {
        setFrontVec(mPrevFrontVec);
    }

    if (MR::diffAngleAbs(mHeadVec, headVecCopy) < mActor->getConst().getTable()->mHeadAngleFixMargin) {
        mHeadVec = headVecCopy;
        MR::normalize(&mHeadVec);
    }

    bool needsFix = mCameraTurnHold ? true : !mActor->mGravitySameAhead;
    if (needsFix && mDrawStates.moveDirKept && !MR::isNearZero(mHeadVec - mGravityFixSideVec) && !MR::isNearZero(mHeadVec + mGravityFixSideVec)) {
        mSideVec = mGravityFixSideVec;
        MR::normalize(&mSideVec);

        f32 angle = marioAcos(startGroundNorm.dot(mRetainGroundNorm));

        Vec3f cross4;
        cross4.cross(mRetainGroundNorm, startGroundNorm);
        MR::normalizeOrZero(&cross4);

        if (MR::isNearZero(cross4) || MR::isNearZero(mRetainGroundNorm + startGroundNorm) || MR::isNearZero(mRetainGroundNorm - startGroundNorm)) {
            mCameraTurnHold = true;
        } else {
            Mtx34f mtx;
            NebulaMtxRotAxisRad(mtx, cross4, angle);
            NebulaMtxMultVec(mtx, mSideVec, mSideVec);
            mRetainGroundNorm = startGroundNorm;
            mGravityFixSideVec = mSideVec;
            mCameraTurnHold = false;
        }

        fixFrontVecFromUpSide();
        return;
    }

    Vec3f cross5;
    cross5.cross(startGroundNorm, mMoveDirOnShadow);
    MR::normalizeOrZero(&cross5);

    if (!MR::isNearZero(cross5)) {
        mGravityFixSideVec = cross5;
    } else {
        mGravityFixSideVec = mSideVec;
    }

    mCameraTurnHold = false;
}

// Nearly matches
void Mario::createMtxDir(MtxPtr pMtx, const Vec3f& rFront, const Vec3f& rUp, const Vec3f& rSide) {
    Vec3f side;
    Vec3f front;
    Vec3f up;
    side = rSide;
    front = rFront;
    up = rUp;
    bool isNormal = !isPlayerModeTeresa();
    if (isNormal) {
        if (mMovementStates.mode2D || mActionStates.circular2D) {
            f32 sideAngleDiff = MR::diffAngleAbs(side, mPrevMtxSide);
            f32 frontAngleDiff = MR::diffAngleAbs(front, mPrevMtxFront);
            f32 upAngleDiff = MR::diffAngleAbs(up, mPrevMtxUp);
            if (sideAngleDiff < mActor->mConst->getTable()->mVibrationAbsorbAngleS) {
                side = mPrevMtxSide;
            } else {
                mPrevMtxSide = side;
            }

            if (frontAngleDiff < mActor->mConst->getTable()->mVibrationAbsorbAngleF) {
                front = mPrevMtxFront;
            } else {
                mPrevMtxFront = front;
            }

            if (upAngleDiff < mActor->mConst->getTable()->mVibrationAbsorbAngleH) {
                up = mPrevMtxUp;
            } else {
                mPrevMtxUp = up;
            }
        }
    }

    pMtx[0][0] = side.x;
    pMtx[1][0] = side.y;
    pMtx[2][0] = side.z;
    pMtx[0][1] = up.x;
    pMtx[1][1] = up.y;
    pMtx[2][1] = up.z;
    pMtx[0][2] = front.x;
    pMtx[1][2] = front.y;
    pMtx[2][2] = front.z;
}

bool Mario::isNonFixHeadVec() const {
    if (isStatusActive(MarioStatus_Swim)) {
        return true;
    }

    if (isStatusActive(MarioStatus_Wall)) {
        return true;
    }

    if (isStatusActive(MarioStatus_SideStep)) {
        return true;
    }

    if (isStatusActive(MarioStatus_Foo)) {
        return true;
    }

    if (isStatusActive(MarioStatus_Stick)) {
        return true;
    }

    if (mMovementStates.launchKind) {
        return true;
    }

    if (mActor->mBaseMtxForced) {
        return true;
    }

    if (isStatusActive(MarioStatus_Talk)) {
        return false;
    }

    return mActor->mInDemo;
}

void Mario::createDirectionMtx(MtxPtr pMtx) {
    Vec3f side;
    Vec3f up;
    Vec3f front;
    up = mPostureUpVec;
    side = mSideVec;
    front = mFrontVec;

    if (mDrawStates.postureSet) {
        MR::normalize(&up);
        front.cross(side, up);
        MR::normalizeOrZero(&front);

        if (MR::isNearZero(front)) {
            front = mFrontVec;
        }

        side.cross(up, front);
        MR::normalizeOrZero(&side);

        if (MR::isNearZero(side)) {
            side = mSideVec;
        }
    } else {
        MR::normalizeOrZero(&up);
        side.cross(up, mFrontVec);
        MR::normalizeOrZero(&side);
        if (MR::isNearZero(side)) {
            side = mSideVec;
        }

        front.cross(side, up);
        MR::normalizeOrZero(&front);
        if (MR::isNearZero(front)) {
            front = mFrontVec;
        }
    }

    createMtxDir(pMtx, front, up, side);
    MR::setMtxTrans(pMtx, 0.0f, 0.0f, 0.0f);
}

bool Mario::createCorrectionMtx(MtxPtr pMtx, Vec3f* pOut) {
    pOut->zero();

    if (isNonFixHeadVec()) {
        NebulaMtxIdentity(pMtx);
        return false;
    }

    if (MR::isNearZero(mCorrectionAngle) && mCorrectionTurnTimer == 0) {
        NebulaMtxIdentity(pMtx);
        mCorrectionEaseTimer = 0;
        mCorrectionAngle = 0.0f;
    } else {
        if (mCorrectionEaseTimer == 0 && mCorrectionTurnTimer == 0) {
            Mtx34f tempMtx;
            Vec3f newFrontVec;
            NebulaMtxRotAxisRad(tempMtx, mHeadVec, mCorrectionAngle);
            NebulaMtxMultVec(tempMtx, mFrontVec, newFrontVec);
            setFrontVecKeepUp(newFrontVec);
            mCorrectionAngle = 0.0f;
        }

        Vec3f vec2(0.0f, 1.0f, 0.0f);

        if (mMovementStates.mode2D && !isStatusActive(MarioStatus_Talk)) {
            bool sameSide = false;
            f32 dot = mFrontVec.dot(getCamDirX());

            if (mPostureUpVec.dot(getCamDirY()) < 0.0f) {
                dot = -dot;
            }

            if (dot < 0.0f && mCorrectionAngle < 0.0f) {
                sameSide = true;
            } else if (dot > 0.0f && mCorrectionAngle > 0.0f) {
                sameSide = true;
            }

            if (sameSide) {
                NebulaMtxRotAxisRad(pMtx, vec2, -mCorrectionAngle);
            } else {
                NebulaMtxRotAxisRad(pMtx, vec2, mCorrectionAngle);
            }
        } else {
            NebulaMtxRotAxisRad(pMtx, vec2, mCorrectionAngle);
        }

        if (mCorrectionTurnTimer != 0) {
            mCorrectionAngle += (MR::pi() - mCorrectionAngle) / static_cast< f32 >(mCorrectionTurnTimer);
            mCorrectionTurnTimer--;
            mCorrectionEaseTimer = 0;
        }

        if (mCorrectionEaseTimer != 0) {
            mCorrectionAngle = mCorrectionAngle * static_cast< f32 >(mCorrectionEaseTimer - 1) / static_cast< f32 >(mCorrectionEaseTimer);
            mCorrectionEaseTimer--;
            if (mCorrectionEaseTimer == 0) {
                setFrontVecKeepUp(mFrontVec);
            }
        }
    }

    return true;
}

void Mario::slopeTiltHead(Vec3f* pVec) {
    if (mMovementStates.onGround) {
        if (mSlipPower > 0.0f && !mMovementStates.slopeSlide) {
            f32 blendFactor = mSlipPower / 5.0f;
            if (blendFactor > 1.0f) {
                blendFactor = 1.0f;
            }

            MR::vecBlend(mGroundNorm, *pVec, pVec, blendFactor);
            MR::normalize(pVec);
        } else if (mDrawStates.slopeRunUp) {
            MR::vecBlend(mGroundNorm, *pVec, pVec, 0.2);
            MR::normalize(pVec);
        }
    }
}

void Mario::fixFrontVecByGravity() {
    if (isNonFixHeadVec()) {
        return;
    }

    Vec3f up = -mAirGravityVec;
    MR::normalize(&up);
    if (mActionStates.inTower) {
        Vec3f side(mPosition);
        side -= mTowerCenter;
        MR::vecKillElement(side, mTowerAxis, &side);
        MR::normalizeOrZero(&side);
        if (mFixSideVec.dot(side) >= 0.0f) {
            mFixSideVec = side;
        } else {
            mFixSideVec = -side;
        }

        if (mSideVec.dot(mFixSideVec) >= 0.0f) {
            mSideVec = mFixSideVec;
            MR::normalize(&mSideVec);
        } else {
            mSideVec = -mFixSideVec;
            MR::normalize(&mSideVec);
        }
    } else if (mHitStates.gravityFrontTurn) {
        Vec3f side2 = up.cross(mFrontVec);
        MR::normalizeOrZero(&side2);
        if (MR::isNearZero(side2) == false) {
            mFixSideVec = side2;
        }
    }

    Vec3f front = mFixSideVec.cross(up);
    if (MR::normalizeOrZero(&front) == false) {
        setFrontVec(front);
        mMoveDirOnShadow = mFrontVec;
        f32 moveSpeed = mMoveFrontVec.length();
        mMoveFrontVec = mFrontVec * moveSpeed;
    }
}

void Mario::fixFrontVecFromUpSide() {
    Vec3f frontVec = mSideVec.cross(mHeadVec);
    MR::normalizeOrZero(&frontVec);
    if (MR::isNearZero(frontVec) == false) {
        setFrontVec(frontVec);
        mMoveDirOnShadow = mFrontVec;
        f32 scaleFactor = mMoveFrontVec.length();
        mMoveFrontVec = mFrontVec * scaleFactor;
    }
}

void Mario::fixSideVecFromFrontUp() {
    Vec3f side = mHeadVec.cross(mFrontVec);
    MR::normalizeOrZero(&side);
    if (MR::isNearZero(side) == false) {
        mSideVec = side;
    }
}

void Mario::setSideVec(const Vec3f& rSide) {
    mSideVec = rSide;
    MR::normalize(&mSideVec);
}

void Mario::setHeadVec(const Vec3f& rHead) {
    mHeadVec = rHead;
    MR::normalize(&mHeadVec);
}

void Mario::setFrontVec(const Vec3f& rFront) {
    Vec3f awayNormal;  // the 2D plane's normal, turned to the side of the camera's z axis
    Vec3f mirrored;
    if (mMovementStates.mode2D && !isStatusActive(MarioStatus_Talk)) {
        if (getCamDirZ().dot(m2DPlaneNormal) > 0.0f) {
            awayNormal = m2DPlaneNormal;
        } else {
            Vec3f flipped = -m2DPlaneNormal;
            awayNormal = flipped;
        }

        if (awayNormal.dot(rFront) > 0.0f) {
            // A front pointing that way is mirrored through the plane.
            f32 along = MR::vecKillElement(rFront, awayNormal, &mirrored);
            mirrored -= awayNormal * along;
            MR::normalizeOrZero(&mirrored);
            mFrontVec = mirrored;
        } else {
            mFrontVec = rFront;
        }
    } else {
        mFrontVec = rFront;
    }

    MR::normalize(&mFrontVec);
}

void Mario::setFrontVecKeepUp(const Vec3f& rFront, f32 blendFactor) {
    Vec3f blendedFront;
    if (MR::isNearZero(rFront) == false) {
        Vec3f normalizedFront(rFront);
        MR::normalize(&normalizedFront);
        s32 blend = 0;
        f32 angleDiff = MR::diffAngleAbs(mFrontVec, normalizedFront);
        if (angleDiff < HALF_PI) {
            blend = MR::vecBlendSphere(mFrontVec, normalizedFront, &blendedFront, blendFactor);
        }

        if (blend == 0) {
            MR::vecRotAxis(mFrontVec, normalizedFront, mHeadVec, &blendedFront, angleDiff * blendFactor);
        }

        setFrontVecKeepUp(blendedFront);
    }
}

void Mario::setFrontVecKeepUpAngle(const Vec3f& rFrontVec, f32 angle) {
    if (MR::isNearZero(rFrontVec) == false) {
        f32 angleDiff = MR::diffAngleAbs(mFrontVec, rFrontVec);
        if (angleDiff != 0.0f) {
            if (angleDiff < angle) {
                setFrontVecKeepUp(rFrontVec);
            } else {
                setFrontVecKeepUp(rFrontVec, angle / angleDiff);
            }
        }
    }
}

void Mario::setFrontVecKeepUp(const Vec3f& rFront, u32 easeFrames) {
    Mtx rotMtx;
    Vec3f rotFrontVec;
    Vec3f rotVec2;
    NebulaMtxRotAxisRad(rotMtx, &mHeadVec, mCorrectionAngle);
    NebulaMtxMultVec(rotMtx, &mFrontVec, &rotFrontVec);
    f32 angleDiff = MR::diffAngleAbs(rFront, rotFrontVec);
    if (angleDiff > 0.024543693f) {
        NebulaMtxRotAxisRad(rotMtx, &mHeadVec, PI_180);
        NebulaMtxMultVec(rotMtx, &rotFrontVec, &rotVec2);
        f32 angleDiff2 = MR::diffAngleAbs(rFront, rotVec2);
        if (angleDiff2 < angleDiff) {
            mCorrectionAngle = -angleDiff;
        } else {
            mCorrectionAngle = angleDiff;
        }
    }

    setFrontVecKeepUp(rFront);
    if (easeFrames != 0) {
        mCorrectionEaseTimer = easeFrames;
    }
}

void Mario::setFrontVecKeepUp(const Vec3f& rFront) {
    Vec3f front = mFrontVec;
    Vec3f side;
    Vec3f blendedFront;
    if (MR::isNearZero(rFront) == false) {
        setFrontVec(rFront);
    }

    side.cross(mHeadVec, mFrontVec);
    if (MR::isNearZero(side)) {
        MR::vecBlendSphere(front, rFront, &blendedFront, 0.5f);
        side.cross(mHeadVec, blendedFront);
        setFrontVec(blendedFront);
        if (MR::isNearZero(side)) {
            setFrontVecKeepSide(rFront);
            return;
        }
    }

    MR::normalize(&side);
    mSideVec = side;
    MR::normalize(&mSideVec);
    Vec3f upright = mSideVec.cross(mHeadVec);
    setFrontVec(upright);
    mMoveDirOnShadow = mFrontVec;
    f32 moveSpeed = mMoveFrontVec.length();
    mMoveFrontVec = mFrontVec * moveSpeed;
    mFixSideVec = mSideVec;
}

void Mario::setFrontVecKeepSide(const Vec3f& rFront) {
    Vec3f headVec;
    Vec3f front;
    if (MR::isNearZero(rFront) == false) {
        setFrontVec(rFront);
    }

    headVec.cross(mFrontVec, mSideVec);
    if (MR::normalizeOrZero(&headVec) != false) {
        const Vec3f* gravity = getGravityVec();
        Vec3f up = -(*gravity);
        mHeadVec = up;
        MR::normalize(&mHeadVec);
    } else {
        mHeadVec = headVec;
        MR::normalize(&mHeadVec);
    }

    front.cross(mSideVec, mHeadVec);
    setFrontVec(front);
    mMoveDirOnShadow = mFrontVec;
    f32 moveSpeed = mMoveFrontVec.length();
    mMoveFrontVec = mFrontVec * moveSpeed;
}

void Mario::setHeadAndFrontVecFromRotate(const Vec3f& rRotate) {
    Vec3f frontVec = Vec3f(0.0f, 0.0f, 1.0f);
    Vec3f headVec = Vec3f(0.0f, 1.0f, 0.0f);
    Mtx mtx;
    MR::makeMtxTR(mtx, 0.0f, 0.0f, 0.0f, rRotate.x, rRotate.y, rRotate.z);
    NebulaMtxMultVec(mtx, &frontVec, &frontVec);
    NebulaMtxMultVecSR(mtx, &headVec, &headVec);
    mHeadVec = headVec;
    MR::normalize(&mHeadVec);
    setFrontVecKeepUp(frontVec);
}

void Mario::forceSetHeadVecKeepSide(const Vec3f& rVec) {
    mHeadVec = rVec;
    mPostureUpVec = rVec;
    if (MR::isSameDirection(rVec, mSideVec)) {
        fixSideVecFromFrontUp();
    }

    fixFrontVecFromUpSide();
    mPrevFrontVec = mFrontVec;
    mCorrectionEaseTimer = 0;
}

void Mario::draw() const {
    if (mCurrentState != nullptr) {
        mCurrentState->draw3D();
    }

    drawTask();
}

void Mario::addTrans(const Vec3f& rShift) {
    mPosition += rShift;
    Vec3f shifted(mLookOfsBody);
    shifted -= rShift;
    if (MR::abs(mLookOfsBody.x) <= MR::abs(shifted.x)) {
        mLookOfsBody.x = 0.0f;
    }

    if (MR::abs(mLookOfsBody.y) <= MR::abs(shifted.y)) {
        mLookOfsBody.y = 0.0f;
    }

    if (MR::abs(mLookOfsBody.z) <= MR::abs(shifted.z)) {
        mLookOfsBody.z = 0.0f;
    }
}

void Mario::setTrans(const Vec3f& rPos) {
    Vec3f reqShift(rPos);
    reqShift -= mPosition;
    mPosition = rPos;
    Vec3f shifted(mLookOfsBody);
    shifted -= reqShift;
    if (MR::abs(mLookOfsBody.x) <= MR::abs(shifted.x)) {
        mLookOfsBody.x = 0.0f;
    }

    if (MR::abs(mLookOfsBody.y) <= MR::abs(shifted.y)) {
        mLookOfsBody.y = 0.0f;
    }

    if (MR::abs(mLookOfsBody.z) <= MR::abs(shifted.z)) {
        mLookOfsBody.z = 0.0f;
    }
}

bool Mario::isEnableRush() const {
    if (isHanging()) {
        return false;
    }

    if (isDamaging()) {
        return false;
    }

    if (isStatusActive(MarioStatus_Flow)) {
        return false;
    }

    if (mMovementStates.hipDrop && mMovementStates.jumping) {
        return false;
    }

    return true;
}

bool Mario::isInvincible() const {
    if (isPlayerModeInvincible()) {
        return true;
    }

    if (mMovementStates.invincible) {
        return true;
    } else {
        return mActor->mMorphTimer != false;
    }
}

void Mario::inputStick() {
    mActor->getStickValue(&mStickPos.x, &mStickPos.y);
    if (mActionStates.zeroStickNextFrame) {
        mActionStates.zeroStickNextFrame = false;
        mStickPos.x = 0.0f;
        mStickPos.y = 0.0f;
    }

    mStickPos.x = 1.5f * mStickPos.x;
    mStickPos.y = 1.5f * mStickPos.y;

    mStickPos.x = MR::clamp(mStickPos.x, -1.0f, 1.0f);
    mStickPos.y = MR::clamp(mStickPos.y, -1.0f, 1.0f);

    mPrevStickPower = mStickPos.z;

    mStickPos.z = MR::sqrt(mStickPos.x * mStickPos.x + mStickPos.y * mStickPos.y);
    mStickPos.z = MR::clamp(mStickPos.z, 0.0f, 1.0f);

    if (MR::isNearZero(mStickPos.z, 0.01f)) {
        mStickPos.z = 0.0f;
    }

    f32 angle = MR::normalizeAngleAbs(MR::atan2(mStickPos.y, mStickPos.x));
    f32 quarterTurn = HALF_PI;
    f32 stickAngleMargin = mActor->getConst().getTable()->mStickAngleMargin;
    u32 quotient = 0;
    while (angle > quarterTurn) {
        angle -= quarterTurn;
        quotient++;
    }

    f32 resultAngle;

    if (angle >= quarterTurn - stickAngleMargin) {
        resultAngle = quarterTurn;
    } else if (angle <= stickAngleMargin) {
        resultAngle = 0.0f;
    } else {
        resultAngle = (quarterTurn / (quarterTurn - 2.0f * stickAngleMargin)) * (angle - stickAngleMargin);
    }

    resultAngle += quarterTurn * static_cast< f32 >(quotient);

    if (!getPlayer()->mActionStates.heavySteeringStick) {
        mStickPos.x = mStickPos.z * MR::cos(resultAngle);
        mStickPos.y = mStickPos.z * MR::sin(resultAngle);
    }

    mPrevStickAngle = mStickAngle;
    mStickAngle = resultAngle;

    if (mMovementStates.mode2D) {
        stick2DadjustGround(mStickPos.x, mStickPos.y);
    }

    if (mActionStates.circular2D) {
        stick2Dadjust(mStickPos.x, mStickPos.y);
    }

    if (mMovementStates.mode25D) {
        update25Dmode();
        updateAxisFromMode(m25DMode);
    }

    if (mActionStates.inTower) {
        mStickPos.y = 0.0f;
    }

    if (mStickPos.z <= 0.01f) {
        mMovementStates.waitStickNeutral = false;
    }

    calcWorldPadDir(&mWorldPadDir, mStickPos.x, mStickPos.y, false);
    MR::normalizeOrZero(&mWorldPadDir);
}

bool Mario::isCeiling() const {
    if (mCeilPolygon->isValid() == false) {
        return false;
    } else {
        return !MR::isNearZero(mCeilNorm);
    }
}

void Mario::setGravityVec(const Vec3f& rGravity) {
    if (MR::isSameDirection(mAirGravityVec, rGravity)) {
        if (MR::isOppositeDirection(mAirGravityVec, rGravity)) {
            mActionStates.gravityChangeSeen = 1;
            mActionStates.gravityFlipPending = 1;
        }

    } else {
        Vec3f axis;
        axis.cross(mAirGravityVec, rGravity);
        MR::normalizeOrZero(&axis);
        f32 frontDot = MR::abs(mFrontVec.dot(axis));
        f32 sideDot = MR::abs(mSideVec.dot(axis));
        f32 headDot = MR::abs(mHeadVec.dot(axis));
        if (frontDot > sideDot && frontDot > headDot) {
            mActionStates.retainInAir = 0;
            mActionStates.gravityFrontTurnPending = 1;
        }

        mActionStates.gravityChangeSeen = 1;
    }

    Vec3f groundDown = -mGroundNorm;
    mGroundDown = groundDown;
    mAirGravityVec = rGravity;
}

void Mario::updateSoundCode() {
    s32 soundCode = -1;
    Mario* pPlayer = getPlayer();
    if (pPlayer->mMovementStates.onGround) {
        if (mGroundPolygon->isValid()) {
            soundCode = MR::getSoundCodeIndex(mGroundPolygon->getAttributes());
        }
    } else if (mShadowPolygon->isValid()) {
        soundCode = MR::getSoundCodeIndex(mShadowPolygon->getAttributes());
    }

    if (isStatusActive(MarioStatus_Wall)) {
        if (mMovementStates.frontWall) {
            soundCode = MR::getSoundCodeIndex(mFrontWallTriangle->getAttributes());
        } else if (mMovementStates.backWall) {
            soundCode = MR::getSoundCodeIndex(mBackWallTriangle->getAttributes());
        } else if (mMovementStates.sideWall) {
            soundCode = MR::getSoundCodeIndex(mSideWallTriangle->getAttributes());
        }
    }

    MR::setMapSondCodeGravity(mActor, soundCode);
}

bool Mario::isForceStopRush() const {
    if (mVerticalSpeed == 0.0f) {
        switch (mFloorCode) {
        case 1:
        case 4:
        case 10:
        case 15:
        case 16:
        case 24:
        case 129:
            return true;
        }
    }

    return false;
}

const Vec3f& Mario::getShadowNorm() const {
    return *mShadowPolygon->getNormal(0);
}

const Vec3f& Mario::getAirGravityVec() const {
    return mAirGravityVec;
}

const Vec3f Mario::getAirFrontVec() const {
    Vec3f airFront;
    MR::vecKillElement(mFrontVec, mAirGravityVec, &airFront);
    if (MR::normalizeOrZero(&airFront)) {
        return mFrontVec;
    } else {
        return airFront;
    }
}

void Mario::initAfterConst() {
    MarioConst* pConst = mActor->mConst;
    mAirWalkTimer = pConst->getTable()->mAirWalkTime;
    mPosition = mActor->mPosition;
    mShadowPos = mPosition;
    mGroundPos = mPosition;
    mMove->initAfter();
    mFoo->init();
    mSwim->init();
    mSafetyPos = mPosition;
    mLastSafetyPos = mPosition;
    NebulaMtxIdentity(mSafetyBaseMtx);
    NebulaMtxIdentity(mLastSafetyBaseMtx);
    mHipDropStartAnim = getAnimationStringPointer("ヒップドロップ開始");
    mHipDropAnim = getAnimationStringPointer("ヒップドロップ");
    mHipDropLandAnim = getAnimationStringPointer("ヒップドロップ着地");
}

void Mario::writeBackPhyisicalVector() {
    f32 slowdown;
    s32 planeCount;
    u32 i;
    if (mHitStates.enforcedMove) {
        Vec3f startPos(mPosition);
        addTrans(mVelocity);
        if (calcDistToCeil(false) < 200.0f) {
            mVelocity.zero();
            stopWalk();
        }

        mPosition = startPos;
    }

    if (mMovementStates.mode2D || mActionStates.circular2D) {
        Vec3f inPlane(mPosition - m2DPlanePos);
        MR::vecKillElement(inPlane, m2DPlaneNormal, &inPlane);
        mPosition = m2DPlanePos + inPlane;
        MR::vecKillElement(mVelocity, m2DPlaneNormal, &mVelocity);
        Vec3f planeStep;
        f32 toCube = MR::vecKillElement(m2DCubePos - m2DPlanePos, m2DPlaneNormal, &planeStep);
        if (toCube > 1.0f) {
            toCube = 1.0f;
        } else if (toCube < -1.0f) {
            toCube = -1.0f;
        }

        planeStep = m2DPlaneNormal;
        planeStep.setLength(toCube);
        m2DPlanePos += planeStep;
    }

    fixPositionInTower();
    if (mMovementStates.jumping) {
        bool rising = false;
        if (isRising()) {
            rising = true;
        }

        if (mActor->mGravityJumpTimer != 0) {
            mTouchedWalls.clear();
        }

        if (!mMovementStates.inputLocked) {
            for (i = 0; i < mTouchedWalls.size(); i++) {
                Vec3f alongWall;
                if (!(MR::vecKillElement(mVelocity, *mTouchedWalls[i].getNormal(0), &alongWall) < 0.0f)) {
                    continue;
                }

                mVelocity = alongWall;

                if (!mHitStates.binderPushed) {
                    Vec3f jumpAlongWall;
                    f32 intoWall = MR::vecKillElement(mJumpVec, *mTouchedWalls[i].getNormal(0), &jumpAlongWall);
                    if (intoWall <= 0.0f) {
                        mJumpVec = jumpAlongWall + *mTouchedWalls[i].getNormal(0) * intoWall * 0.5f;
                    }
                } else if (MR::diffAngleAbs(*getGravityVec(), mJumpVec) < MR::toRadian(6.0f)) {
                    Vec3f wallOut;
                    MR::vecKillElement(*mTouchedWalls[i].getNormal(0), *getGravityVec(), &wallOut);
                    f32 jumpSpeed = mJumpVec.length();
                    MR::normalizeOrZero(&wallOut);
                    mJumpVec += wallOut;
                    mJumpVec.setLength(jumpSpeed);
                }

                if (rising) {
                    if (mTouchedWalls[i].getNormal(0)->dot(*getGravityVec()) > 0.707f) {
                        mActor->sendMsgUpperPunch(mTouchedWalls[i].mSensor);
                    }
                }

                if (isPlayerModeBee() && mActor->mBeeWallWalk == 0) {
                    if (MR::getWallCodeIndex(&mTouchedWalls[i]) == CollisionWallCode_Fur) {
                        Triangle furTriangle = Triangle();
                        MarioActor* pActor = mActor;
                        Vec3f furHit;
                        if (MR::getFirstPolyOnLineToMap(&furHit, &furTriangle, pActor->mCenterPos, -(*mTouchedWalls[i].getNormal(0)) * 300.0f)) {
                            if (MR::getWallCodeIndex(&furTriangle) == CollisionWallCode_Fur) {
                                MarioActor* pActor2 = getActor();
                                pActor2->entryWallWalkMode(furHit, *furTriangle.getNormal(0));
                            }
                        }
                    }
                }
            }
        }
    } else if (isSwimming()) {
        if (!mMovementStates.inputLocked) {
            for (u32 j = 0; j < mTouchedWalls.size(); j++) {
                Vec3f alongWall;
                if (MR::vecKillElement(mVelocity, *mTouchedWalls[j].getNormal(0), &alongWall) < 0.0f) {
                    mVelocity = alongWall;
                    f32 intoWall = MR::vecKillElement(mSwim->mKnockbackVel, *mTouchedWalls[j].getNormal(0), &mSwim->mKnockbackVel);
                    mSwim->mKnockbackVel += *mTouchedWalls[j].getNormal(0) * intoWall * 0.5f;
                }
            }
        }
    } else if (!mHitStates.slopePushed && mActor->mLastPushMove.dot(mGroundNorm) < 0.707f && !MR::isNearZero(mActor->mLastPushMove, 1.0f)) {
        Vec3f pushDir(mActor->mLastPushMove);
        MR::normalizeOrZero(&pushDir);
        Vec3f offPush;
        f32 alongPush = MR::vecKillElement(mVelocity, pushDir, &offPush);
        if (alongPush < 0.0f) {
            mVelocity = offPush;
            MR::normalizeOrZero(&pushDir);
            slowdown = 1.0f - MR::clamp((-pushDir.dot(mFrontVec) - 0.3f) * 1.4285715f, 0.0f, 1.0f);
            if (!mMovementStates.frontWall && slowdown < 0.1f) {
                if (mMovementStates.sideWall) {
                    mVelocity += getSideWallNorm() * -alongPush;
                } else {
                    planeCount = mActor->mBinder->mPlaneNum;
                    if (planeCount != 0) {
                        f32 lowestDot = 0.0f;
                        u32 index = 0;
                        i = 0;
                        for (; i < planeCount; i++) {
                            f32 dot = mActor->mBinder->getPlane(i)->mParentTriangle.getNormal(0)->dot(mFrontVec);
                            if (dot < lowestDot) {
                                index = i;
                                lowestDot = dot;
                            }
                        }

                        const Vec3f& hitPos = mActor->mBinder->getPlane(index)->mHitPos;
                        Vec3f fromHit;
                        MR::vecKillElement(mPosition - hitPos, *getGravityVec(), &fromHit);
                        MR::vecKillElement(fromHit, pushDir, &fromHit);
                        if (!MR::isNearZero(fromHit)) {
                            fromHit.setLength(-alongPush);
                            mVelocity += fromHit;
                        }
                    }
                }
            }

            mTargetWalkSpeedIndex = 0;
            mWalkSpeed *= slowdown;
        }
    }

    mActor->mVelocity = mVelocity;
    mActor->mPosition = mPosition;
}

void Mario::update() {
    updateAndClearStrideParameter();
    checkKeyLock();

    if (!(mMovementStates.jumping && mMovementStates.hipDrop) && mHipDropLandAnim != nullptr && isAnimationRun(mHipDropLandAnim)) {
        mMovementStates.jumping = true;
        mMovementStates.hipDrop = true;
    }

    updateCubeCode();
    mMovementStates.binderHit = updateBinderInfo();

    checkEnforceMove();


    if (!isStatusActive(MarioStatus_Stick) && !isStatusActive(MarioStatus_Hang)) {
        checkBaseTransPoint();
        checkHeadPoint();
    }

    if (mMovementStates.tornado && mTornadoTimer > 1) {
        createAtField(true, 150.0f);
    } else {
        createAtField(false, 40.0f);
    }

    mCeilDist = calcDistToCeil(true);
    mHitStates.ceilingNear = mCeilDist < 160.0f;

    if (mHitStates.ceilingNear && mCeilPolygon->isValid()) {
        damagePolygonCheck(mCeilPolygon);
    }

    if (MR::getKarikariClingNum() != 0) {
        mHitStates.karikariClinging = true;
    }


    f32 wallRadius = 80.0f;

    if (isSwimming()) {
        wallRadius = 90.0f;
    }

    checkAllWall(mActor->mCenterPos, wallRadius);

    updateGroundInfo();

    if (getMovementStates().onGround && !(mMovementStates.jumping && !mMovementStates.hipDrop)) {
        if (damageFloorCheck()) {
            writeBackPhyisicalVector();
            return;
        }

        saveLastSafetyTrans();

        if (isCurrentShadowFloorDangerAction()) {
            Vec3f result;
            Vec3f groundToShadow(mShadowPos - mGroundPos);
            if (MR::vecKillElement(groundToShadow, mAirGravityVec, &result) >= 5.0f && groundToShadow.dot(mFrontVec) > 0.0f) {
                mDrawStates.atEdge = true;
                groundToShadow.setLength(5.0f);
                addVelocity(groundToShadow);
            }
        }
    }

    mDrawStates.wallDamageCheck = true;

    if (damageWallCheck()) {
        return;
    }

    mDrawStates.wallDamageCheck = false;


    if (checkPressDamage()) {
        return;
    }


    if (checkSliderMode()) {
        startSlider();
    }

    checkAndTryForceJump();

    if (mMovementStates.freeJumpDelay) {
        return;
    }

    if (checkStartSwim()) {
        writeBackPhyisicalVector();
        return;
    }

    if (!mMovementStates.inputLocked) {
        check2DMode();
    }

    mFpViewChangeFailed = false;

    if (MR::testFpViewStartTrigger()) {
        if (MR::isPossibleToShiftToFirstPersonCamera()) {
            tryFpViewMode();
        } else if (!MR::isDemoActive()) {
            if (!MR::isEqualStageName("EpilogueDemoStage")) {
                MR::startSystemSE("SE_SY_CAMERA_NG");
            }

            mFpViewChangeFailed = true;
        }
    }

    inputStick();
    checkLockOnHoming();


    actionMain();


    calcFrontFloor();

    if (mMovementStates.slopeSlide && mMovementStates.onGround && mMovementStates.slopeFloorFound && isSlipPolygon(mSlopeFloorPolygon)) {
        f32 dot = MR::vecKillElement(mVelocity, *mSlopeFloorPolygon->getNormal(0), &mVelocity);
        Vec3f minus((mGroundNorm - *mSlopeFloorPolygon->getNormal(0)) * 0.5f);
        mVelocity += minus * MR::abs(dot);
    }


    tryPushToVelocity();
    powerAreaMove();
    powerRailMove();


    addVelocity(mVelocityAfter);


    checkForceGrounding();


    checkStep();
    checkBump();
    doCubeWarp();

    if (isSwimming()) {
        checkBaseTransBall();
    }

    doSpinPunchAroundPolygons();


    writeBackPhyisicalVector();
    updateTimers();
    doExtraServices();
}

void Mario::actionMain() {
    if (checkDamage() == false) {
        if (mCurrentState) {
            sendStateMsg(MarioStateMsg_Update);
        } else if (mMovementStates.jumping) {
            procJump(false);
            checkTornado();
            checkHang();
            checkWallStick();
        } else {
            mainMove();
            updateWalkSpeed();
        }

        if (mMovementStates.onGround) {
            mMovementStates.hangBlocked = 1;
            mMovementStates.hangAllowed = 1;
            doFrontStep();
        }
    }

    if (!mMovementStates.slopeSlide) {
        mPreSlopeVelocity = mVelocity;
        mSlopeTimer = 0;
        mSlopeSideSpeed = 0.0f;
        mSlopeSideDir.zero();
    } else {
        mSlopeTimer++;
    }

    if (mSlopeSteerInhibitTimer != 0) {
        mSlopeSteerInhibitTimer--;
    }

    if (mMovementStates.onGround && !mMovementStates.jumping) {
        if (isEnableSlopeMove()) {
            slopeMove();
        } else {
            clearSlope();
        }
    }

    if (isStickOn() && isAnimationRun("水泳陸うちあげ")) {
        stopAnimation(nullptr);
    }

    if (!mDrawStates.slopeRunUp) {
        mSlopeDashTimer = 0;
    }

    callExtraTasks(0);
    beeMarioOnGround();
    checkSpecialWaitAnimation();
    if (!mDrawStates.retainMoveDirRan) {
        retainMoveDir(mStickPos.x, mStickPos.y, nullptr);
    }
}

void Mario::updateGroundInfo() {
    checkMap();
    if (isEnableCheckGround()) {
        bool wasInAir = !mMovementStates.jumping && !mMovementStates.onGround;

        mMovementStates.onGround = checkGround();

        if (mDrawStates.edgePushBack) {
            mEdgePushTimer++;
        } else {
            mEdgePushTimer = 0;
        }

        if (wasInAir && mMovementStates.onGround && isDefaultAnimationRun("落下")) {
            changeAnimation(nullptr, "基本");
        }

        if (!isStatusActive(MarioStatus_RecoveryOrWarp)) {
            if (!isStatusActive(MarioStatus_RecoveryOrWarp) && !mMovementStates.onGround && mMovementStates.launchKind == 1 && mJumpVec.dot(*getGravityVec()) > 0.0f &&
                mJumpVec.dot(*mShadowPolygon->getNormal(0)) < 0.0f && mPostureUpVec.dot(*getGravityVec()) > 0.0f && mVerticalSpeed < 170.0f) {
                mMovementStates.onGround = true;
                setGroundNorm(*mShadowPolygon->getNormal(0));
                setTrans(mShadowPos);
            }
        }
    }

    if (mMovementStates.onGround) {
        mMovementStates.slipUpJumped = 0;
        if (mSlipInhibitTimer == 0 && isSlipPolygon(mGroundPolygon)) {
            mDrawStates.onSlipFloor = 1;
        }
    }

    if (mSlipInhibitTimer != 0) {
        mSlipInhibitTimer--;
    }

    updateFloorCode();
    updateSoundCode();
    updateOnSand();
    updateOnWater();
    updateOnPoison();
}

bool Mario::postureCtrl(MtxPtr) {
    Vec3f vec1(mPostureUpVec);
    Vec3f vec2(mHeadVec);
    f32 blend;
    f32 headRotateRatio = mActor->getConst().getTable()->mHeadRotateRatio;
    if (mMovementStates.onGround) {
        bool noSlip = !isSlipFloorCode(mFloorCode);
        if (noSlip) {
            if (mTargetWalkSpeedIndex < 3) {
                if (calcAngleD(*mGroundPolygon->getNormal(0)) < 45.0f) {
                    vec2 = -mAirGravityVec;
                } else {
                    f32 pastSteep = calcAngleD(*mGroundPolygon->getNormal(0)) - 45.0f;
                    MR::vecBlendSphere(-mAirGravityVec, vec2, &vec2, pastSteep / 60.0f);
                }
            } else {
                vec2 = -*getGravityVec() - mAirGravityVec;
                MR::normalizeOrZero(&vec2);
            }
        }
    }

    if (isAnimationRun("坂すべり上向きうつぶせ", MarioIndexedAnime_SlideUpFaceDown)) {
        vec2 = mGroundNorm;
    }

    if (isStatusActive(MarioStatus_Wait)) {
        if (isAnimationRun("特殊ウエイト1A")) {
            if (getAnimator()->getFrame() > 290.0f) {
                blend = 1.0f;
            } else if (getAnimator()->getFrame() > 260.0f) {
                blend = 0.5f + 0.5f * ((getAnimator()->getFrame() - 260.0f) / 30.0f);
            } else {
                blend = 0.5f * (getAnimator()->getFrame() / 260.0f);
            }

            MR::vecBlendSphere(vec2, -mGroundDown, &vec2, blend);
        } else if (mHitStates.groundSameSensor && mHitStates.groundEvenSlope) {
            vec2 = -mGroundDown;
        }
    }

    if (!isSwimming() && (isAnimationRun("水泳ジェット") || isAnimationRun("水泳スピン移動") || isAnimationRun("リングダッシュ") ||
                          isAnimationRun("水泳スピンジャンプ"))) {
        MR::vecBlendSphere(mHeadVec, mJumpVec, &vec2, 0.1f);

        if (MR::normalizeOrZero(&vec2)) {
            vec2 = mHeadVec;
        }

        mDrawStates.postureSet = true;
    }

    calcTornadoTilt();

    slopeTiltHead(&vec2);

    if (MR::diffAngleAbs(vec1, vec2) > mActor->getConst().getTable()->mHeadAngleFixMargin && !MR::vecBlendSphere(vec1, vec2, &vec1, headRotateRatio)) {
        Mtx34f mtx;
        NebulaMtxRotAxisRad(mtx, &mSideVec, 0.1f);
        NebulaMtxMultVecSR(mtx, vec1, &vec1);
    }

    if (!MR::normalizeOrZero(&vec1)) {
        mPostureUpVec = vec1;
    }

    return false;
}

void Mario::createAngleMtx(MtxPtr pMtx, bool hostSetBase) {
    if (!(isNonFixHeadVec() || hostSetBase)) {
        fixHeadFrontVecByGravity();
    }

    if (updatePosture(pMtx)) {
        return;
    }

    createDirectionMtx(pMtx);
}

void Mario::updateLookOfs() {
    Vec3f vec;
    vec.zero();

    if (isStatusActive(MarioStatus_Blown)) {
        return;
    }

    if (mActor->mInRush) {
        MR::vecBlend(mLookOfs, vec, &mLookOfs, 0.3f);
    }

    if (mCurrentState == nullptr) {
        if (isAnimationRun("壁押し")) {
            Vec3f vec2;
            MR::vecKillElement(mFrontWallPos - mPosition, mHeadVec, &vec2);
            f32 len = vec2.length();
            vec = *mFrontWallTriangle->getNormal(0) * (len - 35.0f);
        }
    } else if (mCurrentState->mStatusId == MarioStatus_Swim) {
        vec += -*getGravityVec() * mSwim->mSurfaceOffset;
    }

    if (mMovementStates.onGround && !mMovementStates.jumping && mVerticalSpeed > 5.0f && mVerticalSpeed < 50.0f && !isCurrentShadowFloorDangerAction()) {
        Vec3f dirShadowToGround(mGroundPos - mShadowPos);
        MR::normalizeOrZero(&dirShadowToGround);
        if (dirShadowToGround.dot(mGroundNorm) < 0.1f) {
            vec += *getGravityVec() * mVerticalSpeed;
        } else {
            Vec3f vec3;
            f32 toGround = MR::vecKillElement(mGroundPos - mPosition, *getGravityVec(), &vec3);
            vec += *getGravityVec() * toGround;
        }
    }

    Vec3f vec4;
    vec4.zero();

    if (mSinkTimer != 0) {
        vec4 = -mPostureUpVec * static_cast< f32 >(mSinkTimer) * (1.0f / 256.0f) * 120.0f;
    }

    f32 blendRate = 0.2f;

    if (!isUseSimpleGroundCheck()) {
        if (getAnimator()->isLandingAnimationRun()) {
            blendRate = 0.01f;
        } else if (mTargetWalkSpeedIndex == 0 && mWalkSpeed < 0.2f && isBlendWaitGround()) {
            if (mLookOfsBody.length() > vec.length()) {
                blendRate = 0.5f;
            } else {
                blendRate = 0.02f * static_cast< f32 >(mLandTimer);

                if (blendRate >= 0.2f) {
                    blendRate = 0.2f;
                }
            }
        }
    }

    MR::vecBlend(mLookOfsBody, vec, &mLookOfsBody, blendRate);
    MR::vecBlend(mLookOfsSink, vec4, &mLookOfsSink, 0.2f);
    mLookOfs = mLookOfsBody + mLookOfsSink;
}

// conditionals won't behave
const Vec3f* Mario::getGravityVec() const {
    if (isStatusActive(MarioStatus_Bump) || isStatusActive(MarioStatus_Climb)) {
        return &mBumpGravityVec;
    }

    if (mMovementStates.onGround) {
        bool noSlip = !isSlipFloorCode(mFloorCode);
        if (noSlip) {
            if (mActor->mBeeWallWalk != 0) {
                return &mAirGravityVec;
            }

            if (isAnimationRun("ハード着地")) {
                return &mAirGravityVec;
            }

            if (mLandTimer < 0x10 && mAirTimer > 3 && !mMovementStates.hipDrop) {
                return &mAirGravityVec;
            }

            if (MR::isNearZero(mGroundDown) == false && mHitStates.groundSameSensor && mHitStates.groundEvenSlope) {
                return &mGroundDown;
            }
        }
    }

    if (isSwimming()) {
        if (mSwim->mWaterDepth >= -150.0f && mDrawStates.wallGravity && MR::isNearZero(mWallGravityVec) == false) {
            return &mWallGravityVec;
        }

        return &mAirGravityVec;
    }

    bool slipKind = true;
    if (mJumpKind == 0xc) {
        slipKind = false;
    } else if (mJumpKind == 0xd) {
        slipKind = false;
    }

    bool noSlip = !isSlipFloorCode(mFloorCode);
    if (noSlip && slipKind) {
        bool noHopper = !isPlayerModeHopper();
        if (noHopper) {
            bool noTeresa = !isPlayerModeTeresa();
            if (noTeresa && !isDamaging() && !isStatusActive(MarioStatus_Swim) && !isStatusActive(MarioStatus_Blown) &&
                !isStatusActive(MarioStatus_RecoveryOrWarp) && mJumpKind != 5 && mActor->mGravityJumpTimer == 0 && mMovementStates.jumping && !mMovementStates.inputLocked && mAirTimer < 8) {
                return &mGroundDown;
            }
        }
    }

    return &mAirGravityVec;
}

void Mario::touchWater() {
    if (mActor->isActionOk("水解除") && mMorphResetTimer == 0) {
        mMorphResetTimer = 10;
    }
}

void MarioState::draw3D() const {
}
