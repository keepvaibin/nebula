#include "Game/Enemy/WalkerStateRunaway.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"

namespace {
    static WalkerStateRunawayParam sDefaultParam;
};  // namespace

namespace NrvWalkerStateRunaway {
    NEW_NERVE(WalkerStateRunawayNrvWait, WalkerStateRunaway, Wait);
    NEW_NERVE(WalkerStateRunawayNrvRunaway, WalkerStateRunaway, Runaway);
    NEW_NERVE(WalkerStateRunawayNrvWallJump, WalkerStateRunaway, WallJump);
};  // namespace NrvWalkerStateRunaway

WalkerStateRunawayParam::WalkerStateRunawayParam() {
    mWaitAction = "FollowMe";
    mRunawayAction = "Run";
    mJumpAction = "Jump";
    mGroundAccel = 0.05f;
    mAirAccel = 1.0f;
    mRunFriction = 0.9f;
    mWallJumpFriction = 0.99f;
    mTurnMaxRateDegree = 3.0f;
    mRunawayDistance = 1100.0f;
    mWaitDistance = 1300.0f;
    mRunawayTurnRateMaxDegree = 10.0f;
    mRunawayTurnRateMinDegree = 3.0f;
    mRunawayTurnTime = NebulaFrameClock::stepsOfMilliseconds(500);
    mPlayerFrontLineLength = 25.0f;
    mRunawayBckRatio = 1.0f;
    mMinRunawayBckRate = 0.9f;
    mMaxRunawayBckRate = 1.4f;
    mWallJumpTime = 6;
    mWallReboundPower = 0.3f;
    mWallJumpPowerH = 8.0f;
    mWallJumpPowerV = 15.0f;
}

WalkerStateRunaway::WalkerStateRunaway(LiveActor* pHost, Vec3f* pDirection, WalkerStateRunawayParam* pRunawayParam)
    : ActorStateBase< LiveActor >("歩行型アクター逃げ", pHost), mRunawayParam(pRunawayParam), mDirection(pDirection), mRunawaySpeed(1.0f) {
    if (mRunawayParam == nullptr) {
        ::sDefaultParam = WalkerStateRunawayParam();  // again, with a mod's numbers
        mRunawayParam = &::sDefaultParam;
    }

    initNerve(GET_NERVE(WalkerStateRunaway, WalkerStateRunawayNrvWait));
}

void WalkerStateRunaway::appear() {
    mIsDead = false;
    setNerve(GET_NERVE(WalkerStateRunaway, WalkerStateRunawayNrvWait));
}

bool WalkerStateRunaway::tryRunaway() {
    if (MR::isNearPlayer(getHost(), mRunawayParam->mRunawayDistance)) {
        setNerve(GET_NERVE(WalkerStateRunaway, WalkerStateRunawayNrvRunaway));
        return true;
    }

    return false;
}

bool WalkerStateRunaway::tryWait() {
    if (isInWaitRange(mRunawayParam->mWaitDistance)) {
        setNerve(GET_NERVE(WalkerStateRunaway, WalkerStateRunawayNrvWait));
        return true;
    }

    return false;
}

bool WalkerStateRunaway::tryWallJump() {
    if (MR::isBindedWall(getHost()) && MR::calcHitPowerToWall(getHost()) > 0.0f) {
        Vec3f horiz;
        MR::calcVecFromPlayerH(&horiz, getHost());
        Vec3f wallNorml;
        MR::calcWallNormalHorizontal(&wallNorml, getHost());

        if (MR::isOppositeDirection(horiz, wallNorml)) {
            MR::addVelocityClockwiseToDirection(getHost(), wallNorml,
                                                MR::isHalfProbability() ? mRunawayParam->mWallJumpPowerH : -mRunawayParam->mWallJumpPowerH);
        } else {
            Vec3f dir;
            dir.add(wallNorml, horiz);
            MR::addVelocityMoveToDirection(getHost(), dir, mRunawayParam->mWallJumpPowerH);
        }

        MR::addVelocityJump(getHost(), mRunawayParam->mWallJumpPowerV);
        setNerve(GET_NERVE(WalkerStateRunaway, WalkerStateRunawayNrvWallJump));
        return true;
    }

    return false;
}

void WalkerStateRunaway::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(getHost(), mRunawayParam->mWaitAction);
    }

    MR::turnDirectionToPlayerDegree(getHost(), mDirection, mRunawayParam->mTurnMaxRateDegree);
    MR::addVelocityToGravity(getHost(), MR::isBindedGround(getHost()) ? mRunawayParam->mGroundAccel : mRunawayParam->mAirAccel);
    MR::attenuateVelocity(getHost(), mRunawayParam->mRunFriction);

    if (tryRunaway()) {
        return;
    }
}

void WalkerStateRunaway::exeRunaway() {
    if (MR::isFirstStep(this)) {
        MR::startAction(getHost(), mRunawayParam->mRunawayAction);
    }

    // Run from the nearest point of the player's path ahead.
    Vec3f fleeFrom;
    fleeFrom.scaleAdd(mRunawayParam->mPlayerFrontLineLength, *MR::getPlayerVelocity(), *MR::getPlayerPos());
    Vec3f* actorPos = &mHost->mPosition;
    MR::calcPerpendicFootToLineInside(&fleeFrom, *actorPos, *MR::getPlayerPos(), fleeFrom);
    MR::turnDirectionFromTargetDegree(mHost, mDirection, fleeFrom,
                                      MR::calcNerveValue(mHost, mRunawayParam->mRunawayTurnTime, mRunawayParam->mRunawayTurnRateMaxDegree,
                                                         mRunawayParam->mRunawayTurnRateMinDegree));

    MR::setBckRate(getHost(),
                   MR::clamp(mRunawaySpeed / mRunawayParam->mRunawayBckRatio, mRunawayParam->mMinRunawayBckRate, mRunawayParam->mMaxRunawayBckRate));
    MR::addVelocityMoveToDirection(getHost(), *mDirection, mRunawaySpeed);
    MR::addVelocityToGravity(getHost(), MR::isBindedGround(getHost()) ? mRunawayParam->mGroundAccel : mRunawayParam->mAirAccel);
    MR::attenuateVelocity(getHost(), mRunawayParam->mRunFriction);

    if (!tryWait()) {
        if (tryWallJump()) {
            return;
        }
    }
}

void WalkerStateRunaway::exeWallJump() {
    if (MR::isFirstStep(this)) {
        MR::startAction(getHost(), mRunawayParam->mJumpAction);
    }

    MR::turnDirectionDegree(getHost(), mDirection, getHost()->mVelocity, 45.0f);
    MR::addVelocityToGravity(getHost(), mRunawayParam->mAirAccel);
    MR::attenuateVelocity(getHost(), mRunawayParam->mWallJumpFriction);
    MR::reboundVelocityFromEachCollision(getHost(), 0.0f, mRunawayParam->mWallReboundPower, 0.0f, 0.0f);

    if (MR::isGreaterStep(this, mRunawayParam->mWallJumpTime)) {
        if (MR::isBindedGround(getHost())) {
            setNerve(GET_NERVE(WalkerStateRunaway, WalkerStateRunawayNrvRunaway));
        }
    }
}

bool WalkerStateRunaway::isRunning() const {
    return isNerve(GET_NERVE(WalkerStateRunaway, WalkerStateRunawayNrvRunaway));
}
