#include "Game/Boss/DinoPackunAction.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/DinoPackun.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>

namespace {
    // Walks faster than this use the run animation's speed.
    const f32 cRunSpeedMin = 1.2f;
    const s32 cStartSteps = NebulaFrameClock::stepsOfSeconds(1);
};  // namespace

DinoPackunAction::DinoPackunAction(const char* pName, DinoPackun* pHost) : ActorStateBase< DinoPackun >(pName, pHost), mSide(1.0f) {
}

// Moving into the receiver blows it away; otherwise it is a plain hit.
bool DinoPackunAction::sendBlowAttackMessage(HitSensor* pSender, HitSensor* pReceiver, bool isFire) {
    Vec3f direction;
    MR::calcSensorDirection(&direction, pSender, pReceiver);

    if (mHost->mVelocity.dot(direction) >= 0.0f) {
        if (isFire) {
            return MR::sendMsgEnemyAttackFireStrong(pReceiver, pSender);
        }

        return MR::sendMsgEnemyAttackMaximum(pReceiver, pSender);
    }

    return sendHitAttackMessage(pSender, pReceiver, isFire);
}

bool DinoPackunAction::sendHitAttackMessage(HitSensor* pSender, HitSensor* pReceiver, bool isFire) {
    if (isFire) {
        return MR::sendMsgEnemyAttackFire(pReceiver, pSender);
    }

    return MR::sendMsgEnemyAttackStrong(pReceiver, pSender);
}

// Turns toward mSide (1 right, -1 left) for `turnSteps`.
bool DinoPackunAction::updateTurn(s32 turnSteps, f32 turnDegree) {
    Vec3f side;
    MR::calcSideVec(&side, mHost);
    Vec3f direction(side * mSide);
    MR::turnDirectionDegree(mHost, &mHost->mFront, direction, turnDegree);
    mHost->updateNormalVelocity();
    return MR::isGreaterStep(this, turnSteps);
}

// Walks ahead for `walkSteps`, a footprint every half `footPrintPeriod`, then picks the next turn.
bool DinoPackunAction::updateWalk(s32 walkSteps, f32 speed, s32 footPrintPeriod) {
    MR::addVelocityMoveToDirection(mHost, mHost->mFront, speed);

    if (speed > cRunSpeedMin) {
        mHost->updateRunVelocity();
    } else {
        mHost->updateNormalVelocity();
    }

    mHost->updateFootPrintNerve(getNerveStep(), footPrintPeriod);

    if (MR::isGreaterStep(this, walkSteps)) {
        selectTurnDirection();
        return true;
    }

    return false;
}

// Runs at the player for `chaseSteps`, turning while the player is within `faceDegree` of ahead, speeding up over
// `accelSteps`.
bool DinoPackunAction::updateChase(s32 chaseSteps, f32 faceDegree, f32 turnDegree, f32 speed, s32 accelSteps, s32 footPrintPeriod) {
    if (MR::isFaceToPlayerDegree(mHost, mHost->mFront, faceDegree)) {
        MR::turnDirectionToPlayerDegree(mHost, &mHost->mFront, turnDegree);
    }

    mHost->updateFootPrintNerve(getNerveStep(), footPrintPeriod);
    MR::addVelocityMoveToDirection(mHost, mHost->mFront, (speed * MR::calcNerveRate(this, accelSteps)));
    mHost->updateRunVelocity();
    return MR::isGreaterStep(this, chaseSteps);
}

void DinoPackunAction::selectTurnDirection() {
    mSide = MR::isHalfProbability() ? 1.0f : -1.0f;
}

bool DinoPackunAction::updateStart() {
    mHost->updateNormalVelocity();

    if (MR::isBckOneTimeAndStopped(mHost)) {
        MR::startBck(mHost, "Wait");
    }

    return MR::isGreaterStep(this, cStartSteps);
}

// Roars and turns to the player; it lands from the jump at `landStep`.
bool DinoPackunAction::updateFind(s32 landStep, f32 turnDegree) {
    if (MR::isFirstStep(this)) {
        MR::startBck(mHost, "Find");
        MR::startSound(mHost, "SE_BV_D_PAKKUN_FIND");
        MR::startSound(mHost, "SE_BM_D_PAKKUN_SLAVER");
    }

    if (MR::isStep(this, landStep)) {
        MR::startSound(mHost, "SE_BM_D_PAKKUN_LAND");
    }

    MR::turnDirectionToPlayerDegree(mHost, &mHost->mFront, turnDegree);
    mHost->updateNormalVelocity();
    return MR::isBckStopped(mHost);
}

bool DinoPackunAction::updateCoolDown(s32 coolDownSteps) {
    if (MR::isFirstStep(this)) {
        MR::startBck(mHost, "CoolDown");
    }

    mHost->updateNormalVelocity();
    return MR::isGreaterStep(this, coolDownSteps);
}

bool DinoPackunAction::updateAttackHit() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "AttackHit");
        MR::startSound(mHost, "SE_BV_D_PAKKUN_GLAD");
        MR::startSound(mHost, "SE_BM_D_PAKKUN_SLAVER");
    }

    mHost->updateNormalVelocity();
    return MR::isActionEnd(mHost);
}
