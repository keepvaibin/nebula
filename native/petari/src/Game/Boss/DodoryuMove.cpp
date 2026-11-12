#include "Game/Boss/DodoryuMove.hpp"
#include "Game/Boss/Dodoryu.hpp"
#include "Game/Boss/DodoryuUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"

namespace {
    const f32 sRailAccel = 0.05f;
    const f32 sEscapeAccel = 0.05f;
};  // namespace

namespace DodoryuSub {
    MoveState::MoveState(MoveStateHolder* pHolder) : mHolder(pHolder), mHost(pHolder->mHost), mStep() {
    }

    MoveStateHolder::MoveStateHolder(Dodoryu* pHost) : mHost(pHost) {
        mRailState = new MoveStateRail(this);
        mChaseState = new MoveStateChase(this);
        mDetourState = new MoveStateDetour(this);
        mEscapeState = new MoveStateEscape(this);
        mJumpOutState = new MoveStateJumpOut(this);
        mNullState = new MoveStateNull(this);
        mCurrentState = mNullState;
        mNextState = nullptr;
        mCurrentState->mStep = 0;
    }

    void MoveStateHolder::execute() {
        if (mNextState != nullptr) {
            mNextState->mStep = 0;
            mCurrentState = mNextState;
            mNextState = nullptr;
        } else {
            mCurrentState->mStep++;
        }

        mCurrentState->execute();
    }

    void MoveStateHolder::shiftRail(f32 speed) {
        mNextState = mRailState;
        mRailState->mSpeed = speed;
    }

    // Chase and detour share the tuning, so a detour taken mid-chase keeps it.
    void MoveStateHolder::shiftChase(f32 accel, f32 lateralDamping, f32 maxSpeed, f32 turnAngle) {
        if (mCurrentState != mChaseState && mCurrentState != mDetourState) {
            mNextState = mChaseState;
        }

        mChaseState->mAccel = accel;
        mChaseState->mLateralDamping = lateralDamping;
        mChaseState->mMaxSpeed = maxSpeed;
        mChaseState->mTurnAngle = turnAngle;

        mDetourState->mAccel = accel;
        mDetourState->mLateralDamping = lateralDamping;
        mDetourState->mMaxSpeed = maxSpeed;
        mDetourState->mTurnAngle = turnAngle;
    }

    void MoveStateHolder::shiftEscape(f32 speed, s32 dirStepMin, s32 dirStepMax, f32 dirAngleMax, f32 turnAngle) {
        if (mCurrentState != mEscapeState) {
            mNextState = mEscapeState;
            mEscapeState->mDirStepLeft = 0;
        }

        mEscapeState->mMaxSpeed = speed;
        mEscapeState->mSpeed = speed;
        mEscapeState->mDirStepMin = dirStepMin;
        mEscapeState->mDirStepMax = dirStepMax;
        mEscapeState->mDirAngleMax = dirAngleMax;
        mEscapeState->mTurnAngle = turnAngle;
    }

    void MoveStateHolder::shiftJumpOut(f32 speed, f32 turnAngle) {
        if (mCurrentState != mJumpOutState) {
            mNextState = mJumpOutState;
        }

        mJumpOutState->mTurnAngle = turnAngle;
        mJumpOutState->mSpeed = speed;
    }

    void MoveStateHolder::shiftNull() {
        mNextState = mNullState;
    }

    void MoveStateHolder::brake() {
        if (mCurrentState == mEscapeState) {
            mEscapeState->mSpeed = 0.0f;
        } else if (mCurrentState == mChaseState) {
            mChaseState->mHost->mVelocity.zero();
        } else if (mCurrentState == mRailState) {
            mRailState->mSpeed = 0.0f;
        }
    }

    void MoveStateHolder::setRailSpeed(f32 speed) {
        mRailState->mSpeed = speed;
    }

    MoveStateRail::MoveStateRail(MoveStateHolder* pHolder) : MoveState(pHolder), mMaxSpeed(8.0f), mSpeed(8.0f) {
    }

    void MoveStateRail::execute() {
        mSpeed += ::sRailAccel;

        if (mSpeed > mMaxSpeed) {
            mSpeed = mMaxSpeed;
        }

        MR::moveCoord(mHost, mSpeed);
        Pos3f baseMtx = mHost->mBaseMtx;
        Vec3f front;
        baseMtx.getZDir(front);

        Rot3f rotation;
        rotation.setRotate(front, MR::getRailDirection(mHost));
        baseMtx.concat(rotation, baseMtx);
        baseMtx.setTrans(MR::getRailPos(mHost));
        mHost->setMtx(baseMtx);

        mHost->snapToGround();
    }

    void MoveStateChase::execute() {
        Vec3f toPlayer = *MR::getPlayerPos() - mHost->mPosition;

        if (DodoryuUtil::calcVerticalizedDir(mHost, &toPlayer, toPlayer)) {
            DodoryuUtil::turnToward(mHost, toPlayer, mTurnAngle);

            Vec3f accelDir;
            mHost->mBaseMtx.getZDir(accelDir);

            DodoryuUtil::accelerate(mHost, accelDir, mAccel, mLateralDamping, mMaxSpeed);
        }

        if (mHost->checkWallWithVelocity()) {
            mHolder->mNextState = mHolder->mDetourState;

            DodoryuUtil::rotateVelocityByWall(mHost);
        } else {
            DodoryuUtil::addVelocity(mHost, true);
        }
    }

    MoveStateDetour::MoveStateDetour(MoveStateHolder* pHolder) : MoveState(pHolder) {
    }

    void MoveStateDetour::execute() {
        Vec3f accelDir = mHost->mVelocity;

        if (!MR::isNearZero(accelDir)) {
            MR::normalize(&accelDir);
            DodoryuUtil::turnToward(mHost, accelDir, 1.8f);
        } else {
            Vec3f yDir;
            mHost->mBaseMtx.getYDir(yDir);

            accelDir.cross(yDir, mHost->mWallNormal);
        }

        DodoryuUtil::accelerate(mHost, accelDir, mAccel, mLateralDamping, mMaxSpeed);
        DodoryuUtil::addVelocity(mHost, false);

        if (!mHost->snapToWall()) {
            mHolder->mNextState = mHolder->mChaseState;
            mHost->snapToGround();
        } else {
            mHost->snapToGround();
            DodoryuUtil::rotateVelocityByWall(mHost);

            if (isPlayerOppositeFromWall()) {
                mHolder->mNextState = mHolder->mChaseState;
            }
        }
    }

    bool MoveStateDetour::isPlayerOppositeFromWall() const {
        Vec3f toPlayer = *MR::getPlayerPos() - mHost->mPosition;

        return mHost->mWallNormal.dot(toPlayer) > 0.0f;
    }

    MoveStateEscape::MoveStateEscape(MoveStateHolder* pHolder)
        : MoveState(pHolder), mDirStepLeft(), mDirStepMin(60), mDirStepMax(60), mDirAngleMax(), mTurnAngle(), mSpeed(), mDir(0.0f, 0.0f, 0.0f) {
    }

    void MoveStateEscape::execute() {
        if (mDirStepLeft == 0) {
            mDirStepLeft = MR::getRandom(mDirStepMin, mDirStepMax + 1);

            DodoryuUtil::calcEscapeDir(mHost, &mDir, mDirAngleMax);
        }

        mSpeed += ::sEscapeAccel;

        if (mSpeed > mMaxSpeed) {
            mSpeed = mMaxSpeed;
        }

        DodoryuUtil::turnToward(mHost, mDir, mTurnAngle);

        Vec3f newVelocity;
        mHost->mBaseMtx.getZDir(newVelocity);
        newVelocity.setLength(mSpeed);

        mHost->mVelocity.set(newVelocity);

        if (mHost->checkWallWithVelocity()) {
            DodoryuUtil::rotateVelocityByWall(mHost);
        }

        DodoryuUtil::addVelocity(mHost, true);
        mHost->mVelocity.zero();

        mDirStepLeft--;

        if (mDirStepLeft < 0) {
            mDirStepLeft = 0;
        }
    }

    MoveStateJumpOut::MoveStateJumpOut(MoveStateHolder* pHolder) : MoveState(pHolder), mTurnAngle(3.0f), mSpeed(1.0f) {
    }

    void MoveStateJumpOut::execute() {
        const Vec3f& rPosition = mHost->mPosition;
        Vec3f awayFromPlayer = rPosition - *MR::getPlayerPos();

        DodoryuUtil::calcVerticalizedDir(mHost, &awayFromPlayer, awayFromPlayer);
        DodoryuUtil::turnToward(mHost, awayFromPlayer, mTurnAngle);

        Dodoryu* pHost = mHost;
        pHost->mVelocity.set(awayFromPlayer * mSpeed);

        if (mHost->checkWallWithVelocity()) {
            DodoryuUtil::rotateVelocityByWall(mHost);
        }

        DodoryuUtil::addVelocity(mHost, true);
        mHost->mVelocity.zero();
    }

    MoveStateNull::MoveStateNull(MoveStateHolder* pHolder) : MoveState(pHolder) {
    }

    void MoveStateNull::execute() {
    }
};  // namespace DodoryuSub
