#include "Game/MapObj/BallRail.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 1000.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_init_setRailCoordSpeed_1, "init.setRailCoordSpeed.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSetUp_setRailCoordSpeed_1, "exeSetUp.setRailCoordSpeed.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRun_setRailCoordSpeed_1, "exeRun.setRailCoordSpeed.1", 0.0f);

namespace NrvBallRail {
    NEW_NERVE(BallRailNrvWait, BallRail, Wait);
    NEW_NERVE(BallRailNrvSetUp, BallRail, SetUp);
    NEW_NERVE(BallRailNrvRun, BallRail, Run);
    NEW_NERVE(BallRailNrvNoBind, BallRail, NoBind);
};  // namespace NrvBallRail

namespace {
    NEBULA_TUNABLE(s32, sSetUpBlendSteps, 15);
    NEBULA_TUNABLE(s32, sSetUpSteps, 45);
    const s32 sNoBindSteps = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, sSetUpSpeed, 3.0f);
    NEBULA_TUNABLE(f32, sMinRunSpeed, 6.0f);
};  // namespace

BallRail::BallRail(const char* pName)
    : LiveActor(pName), mBindSensor(nullptr), mAcceleration(1.5f), mDeceleration(0.995f), mBindSensorRadius(100.0f) {
    lumacore::setClass(this, lumacore::kClassId< BallRail >);
}

// Arg 0 the gravity acceleration and arg 1 the speed kept each step, both in thousandths.
void BallRail::init(const TableIter& rIter) {
    if (MR::getPlacementArg0NoInit(rIter, &mAcceleration)) {
        mAcceleration /= cNum_init_f1;
    }

    if (MR::getPlacementArg1NoInit(rIter, &mDeceleration)) {
        mDeceleration /= cNum_init_f2;
    }

    MR::connectToSceneMapObjMovement(this);
    MR::invalidateClipping(this);
    MR::onCalcGravity(this);
    initRailRider(rIter);
    MR::moveCoordToStartPos(this);
    MR::setRailCoordSpeed(this, cNum_init_setRailCoordSpeed_1);
    initHitSensor(1);
    MR::addHitSensor(this, "bind", ATYPE_BALL_RAIL, 8, mBindSensorRadius, Vec3f(0.0f, mBindSensorRadius, 0.0f));
    initNerve(GET_NERVE(BallRail, BallRailNrvWait));
    appear();
}

void BallRail::control() {
}

// A ball coming in takes the rail; one moving away, or arriving from below, does not (the one from below stops).
bool BallRail::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg != ACTMES_SPHERE_PLAYER_BINDED || !isNerve(GET_NERVE(BallRail, BallRailNrvWait))) {
        return false;
    }

    Vec3f ballVelocity(pSender->mHost->mVelocity);
    Vec3f fromRail = pSender->mPosition - pReceiver->mPosition;

    if (fromRail.dot(ballVelocity) > 0.0f) {
        return false;
    }

    if (mGravity.dot(fromRail) > 0.0f) {
        MR::zeroVelocity(pSender->mHost);
        return false;
    }

    mBindSensor = pSender;
    return true;
}

void BallRail::exeWait() {
    MR::moveCoordToNearestPos(this, *MR::getPlayerPos());
    MR::moveTransToCurrentRailPos(this);

    if (mBindSensor != nullptr) {
        setNerve(GET_NERVE(BallRail, BallRailNrvSetUp));
    }
}

// Eases the ball onto the rail, riding it slowly.
void BallRail::exeSetUp() {
    if (MR::isFirstStep(this)) {
        mBindStartPos = mBindSensor->mPosition;
        MR::zeroVelocity(mBindSensor->mHost);
        MR::setRailCoordSpeed(this, cNum_exeSetUp_setRailCoordSpeed_1);
    }

    MR::moveCoord(this, sSetUpSpeed);
    MR::moveTransToCurrentRailPos(this);

    Vec3f ballPos;
    if (MR::isLessStep(this, sSetUpBlendSteps)) {
        Vec3f onRail;
        onRail.scaleAdd(-mBindSensor->mRadius, mGravity, mPosition);
        f32 rate = MR::calcNerveEaseInRate(this, sSetUpBlendSteps);
        MR::vecBlend(mBindStartPos, onRail, &ballPos, rate);
    } else {
        ballPos.scaleAdd(-mBindSensor->mRadius, mGravity, mPosition);
    }

    if (MR::isStep(this, sSetUpBlendSteps)) {
        MR::tryRumblePadStrong(this, PLAYER_1);
    }

    mBindSensor->mHost->mVelocity.set(ballPos - mBindSensor->mPosition);

    if (MR::isGreaterStep(this, sSetUpSteps)) {
        setNerve(GET_NERVE(BallRail, BallRailNrvRun));
    }
}

// Gravity along the rail speeds the ball up, never below the minimum and always toward the end; at the goal it leaves
// at the rail's speed.
void BallRail::exeRun() {
    if (MR::isFirstStep(this)) {
        MR::setRailCoordSpeed(this, cNum_exeRun_setRailCoordSpeed_1);
    }

    Vec3f railDir = MR::getRailDirection(this);
    Vec3f acceleration = mGravity * mAcceleration;
    MR::accelerateRailCoordSpeed(this, acceleration.dot(railDir));
    MR::slowDownRailCoordSpeed(this, mDeceleration);

    if (!MR::isRailGoingToEnd(this) || MR::getRailCoordSpeed(this) < sMinRunSpeed) {
        if (!MR::isRailGoingToEnd(this)) {
            MR::setRailDirectionToEnd(this);
        }

        MR::setRailCoordSpeed(this, sMinRunSpeed);
    }

    MR::moveRailRider(this);
    MR::moveTransToCurrentRailPos(this);
    Vec3f ballPos;
    ballPos.scaleAdd(-mBindSensor->mRadius, mGravity, mPosition);
    mBindSensor->mHost->mVelocity.set(ballPos - mBindSensor->mPosition);

    if (MR::isRailReachedGoal(this)) {
        mBindSensor->mHost->mVelocity.set(MR::getRailDirection(this) * MR::getRailCoordSpeed(this));
        mBindSensor->receiveMessage(ACTMES_END_BALL_RAIL, getSensor("bind"));
        mBindSensor = nullptr;
        setNerve(GET_NERVE(BallRail, BallRailNrvNoBind));
    }
}

void BallRail::exeNoBind() {
    if (getNerveStep() > sNoBindSteps) {
        setNerve(GET_NERVE(BallRail, BallRailNrvWait));
    }
}
