#include "Game/Boss/DodoryuMove.hpp"
#include "Game/Boss/Dodoryu.hpp"
#include "Game/Boss/DodoryuUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"

namespace {
    static const f32 sRailAccel = 0.05f;
    static const f32 sEscapeAccel = 0.05f;
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

    void MoveStateHolder::shiftRail(f32 param1) {
        mNextState = mRailState;
        mRailState->mSpeed = param1;
    }

    void MoveStateHolder::shiftChase(f32 param1, f32 param2, f32 param3, f32 param4) {
        if (mCurrentState != mChaseState && mCurrentState != mDetourState) {
            mNextState = mChaseState;
        }

        MoveStateChase* pChaseState = mChaseState;

        pChaseState->mAccel = param1;
        pChaseState->mLateralDamping = param2;
        pChaseState->mMaxSpeed = param3;
        pChaseState->mTurnAngle = param4;

        MoveStateDetour* pDetourState = mDetourState;

        pDetourState->mAccel = param1;
        pDetourState->mLateralDamping = param2;
        pDetourState->mMaxSpeed = param3;
        pDetourState->mTurnAngle = param4;
    }

    void MoveStateHolder::shiftEscape(f32 param1, s32 param2, s32 param3, f32 param4, f32 param5) {
        if (mCurrentState != mEscapeState) {
            mNextState = mEscapeState;
            mEscapeState->mDirStepLeft = 0;
        }

        MoveStateEscape* pEscapeState = mEscapeState;

        pEscapeState->mMaxSpeed = param1;
        pEscapeState->mSpeed = param1;
        pEscapeState->mDirStepMin = param2;
        pEscapeState->mDirStepMax = param3;
        pEscapeState->mDirAngleMax = param4;
        pEscapeState->mTurnAngle = param5;
    }

    void MoveStateHolder::shiftJumpOut(f32 param1, f32 param2) {
        if (mCurrentState != mJumpOutState) {
            mNextState = mJumpOutState;
        }

        MoveStateJumpOut* pJumpOutState = mJumpOutState;

        pJumpOutState->mTurnAngle = param2;
        pJumpOutState->mSpeed = param1;
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
