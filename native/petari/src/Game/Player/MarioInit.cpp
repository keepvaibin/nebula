#include "Game/Player/Mario.hpp"

void Mario::initMember() {
    NebulaMtxIdentity(m2DRotateMtx.toMtxPtr());
    mPosition.zero();
    mPrevVelocity.zero();
    mVelocityAfter.zero();
    mTurnLeft = 0;
    mSlopeSideSpeed = 0.0f;
    mSlopeSideDir.zero();
    mSlipTurnMove.zero();
    mWallSlope.zero();
    mForceJumpVec.zero();
    mShadowPos.zero();

    mCeilNorm.zero();
    mCeilDistNorm.zero();

    mHipDropStillTimer = 0;
    mInvincibleJump = 0;
    mRocketBoostVec.zero();

    mRocketBoostDecay = 0.0f;
    mGroundPos.zero();
    mSlopeFloorPos.zero();
    mLastGroundLocalPos.zero();
    mStepHitPos.zero();
    mBumpHitPos.zero();
    mOnimasuLocalPos.zero();
    mOnimasuFound = 0;
    m2DGroundNorm.zero();
    m2DPlanePos.zero();
    m2DCubePos.zero();

    m25DStick.y = 0.0f;
    m25DStick.x = 0.0f;
    m25DNormal.zero();
    m25DUp.zero();
    m25DAcross.zero();
    mTowerCenter.zero();
    mTowerAxis.zero();
    mHipDropSlideGravity.zero();

    mTowerRadius = 0.0f;
    mBrakeTimer = 0;
    mPrevWalkSpeedIndex = 0;
    mHipDropStartAnim = nullptr;
    mHipDropAnim = nullptr;
    mHipDropLandAnim = nullptr;

    mWallTopHead.zero();
    mBumpGravityVec.zero();
    mDamageDir.zero();
    mSafetyPos.zero();
    NebulaMtxIdentity(mSafetyBaseMtx.toMtxPtr());
    mLastSafetyPos.zero();
    NebulaMtxIdentity(mLastSafetyBaseMtx.toMtxPtr());

    mRelativeWallPos[0].zero();
    mRelativeWallPos[1].zero();
    mEnforceJumpVec.zero();

    mSlopeDashSpeed = 0.0f;

    for (int i = 0; i < ARRAY_SIZE(mExtraTaskFlags); i++) {
        mExtraTaskFlags[i] = 0;
    }

    mTornadoCenteringTimer = 0;
    mSlideTimer = 0;

    mSlideVec.zero();
    mHipDropSlideSensorStart.zero();
    mHipDropSlideSide.zero();
    mHipDropSlideSpeed = 0.0f;

    for (int i = 0; i < ARRAY_SIZE(mIndexedAnimeOn); i++) {
        mIndexedAnimeOn[i] = 0;
    }
}
