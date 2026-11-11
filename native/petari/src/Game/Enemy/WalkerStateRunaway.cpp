#include "Game/Enemy/WalkerStateRunaway.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f1, "WalkerStateRunawayParam.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f2, "WalkerStateRunawayParam.f2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f3, "WalkerStateRunawayParam.f3", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f4, "WalkerStateRunawayParam.f4", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f5, "WalkerStateRunawayParam.f5", 1100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f6, "WalkerStateRunawayParam.f6", 1300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f7, "WalkerStateRunawayParam.f7", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f8, "WalkerStateRunawayParam.f8", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f9, "WalkerStateRunawayParam.f9", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f10, "WalkerStateRunawayParam.f10", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f11, "WalkerStateRunawayParam.f11", 1.4f);
NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f12, "WalkerStateRunawayParam.f12", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f13, "WalkerStateRunawayParam.f13", 8.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_WalkerStateRunawayParam_f14, "WalkerStateRunawayParam.f14", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWallJump_f1, "exeWallJump.f1", 45.0f);

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
    mGroundAccel = cNum_WalkerStateRunawayParam_f1;
    mAirAccel = 1.0f;
    mRunFriction = cNum_WalkerStateRunawayParam_f2;
    mWallJumpFriction = cNum_WalkerStateRunawayParam_f3;
    mTurnMaxRateDegree = cNum_WalkerStateRunawayParam_f4;
    mRunawayDistance = cNum_WalkerStateRunawayParam_f5;
    mWaitDistance = cNum_WalkerStateRunawayParam_f6;
    mRunawayTurnRateMaxDegree = cNum_WalkerStateRunawayParam_f7;
    mRunawayTurnRateMinDegree = cNum_WalkerStateRunawayParam_f8;
    mRunawayTurnTime = NebulaFrameClock::stepsOfMilliseconds(500);
    mPlayerFrontLineLength = cNum_WalkerStateRunawayParam_f9;
    mRunawayBckRatio = 1.0f;
    mMinRunawayBckRate = cNum_WalkerStateRunawayParam_f10;
    mMaxRunawayBckRate = cNum_WalkerStateRunawayParam_f11;
    mWallJumpTime = 6;
    mWallReboundPower = cNum_WalkerStateRunawayParam_f12;
    mWallJumpPowerH = cNum_WalkerStateRunawayParam_f13;
    mWallJumpPowerV = cNum_WalkerStateRunawayParam_f14;
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

    MR::turnDirectionDegree(getHost(), mDirection, getHost()->mVelocity, cNum_exeWallJump_f1);
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
