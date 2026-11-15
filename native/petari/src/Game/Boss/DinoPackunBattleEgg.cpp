#include "Game/Boss/DinoPackunBattleEgg.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/DinoPackun.hpp"
#include "Game/Boss/DinoPackunStateDamage.hpp"
#include "Game/Boss/DinoPackunTail.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <nebula_geometry.hpp>

namespace {
    // Where the tail leaves the egg, in the boss's frame.
    const Vec3f cEggOutPosition(0.0f, 60.0f, -320.0f);
    const s32 cTurnSteps = NebulaFrameClock::stepsOfSeconds(2);
    const f32 cTurnDegree = 0.3f;
    const s32 cWalkSteps = NebulaFrameClock::stepsOfSeconds(5);
    const f32 cWalkSpeed = 0.5f;
    const s32 cWalkFootPrintPeriod = 91;

    // Drops the part of the velocity along `rDir`.
    void removeVelocity(DinoPackun* pHost, const Vec3f& rDir) {
        Vec3f* pVelocity = &pHost->mVelocity;
        pHost->mVelocity.scaleAdd(-rDir.dot(*pVelocity), rDir, *pVelocity);
    }
};  // namespace

namespace NrvDinoPackunBattleEgg {
    NEW_NERVE(DinoPackunBattleEggNrvTurn, DinoPackunBattleEgg, Turn);
    NEW_NERVE(DinoPackunBattleEggNrvWalk, DinoPackunBattleEgg, Walk);
    NEW_NERVE(DinoPackunBattleEggNrvDamage, DinoPackunBattleEgg, Damage);
};  // namespace NrvDinoPackunBattleEgg

// The first fight's opening: the boss walks inside its egg, only the tail out; the egg is harmless and pushes.
DinoPackunBattleEgg::DinoPackunBattleEgg(DinoPackun* pHost) : DinoPackunAction("1回戦卵バトル", pHost), mStateDamage(nullptr) {
}

void DinoPackunBattleEgg::init() {
    initNerve(GET_NERVE(DinoPackunBattleEgg, DinoPackunBattleEggNrvTurn));
    mStateDamage = new DinoPackunStateDamage(getHost());
    mStateDamage->setDamageEgg();
}

void DinoPackunBattleEgg::appear() {
    mIsDead = false;
    getHost()->mTail->lockNodePosition(1);
    setNerve(GET_NERVE(DinoPackunBattleEgg, DinoPackunBattleEggNrvTurn));
}

void DinoPackunBattleEgg::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (getHost()->isSensorEgg(pSender) && MR::isSensorPlayer(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

// The egg takes reflected star pieces.
bool DinoPackunBattleEgg::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    return getHost()->isSensorEgg(pReceiver) && MR::isMsgStarPieceReflect(msg);
}

// A map object stops the egg's motion into it, and turns it when it walks into one.
bool DinoPackunBattleEgg::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (!getHost()->isSensorEgg(pReceiver)) {
        return false;
    }

    if (!(isNerve(GET_NERVE(DinoPackunBattleEgg, DinoPackunBattleEggNrvWalk)) || isNerve(GET_NERVE(DinoPackunBattleEgg, DinoPackunBattleEggNrvTurn))) ||
        !MR::isSensorMapObj(pSender)) {
        return false;
    }

    Vec3f away;
    MR::calcSensorHorizonNormalize(&away, getHost()->mGravity, pSender, pReceiver);

    if (getHost()->mVelocity.dot(away) < 0.0f) {
        removeVelocity(getHost(), away);
    }

    if (isNerve(GET_NERVE(DinoPackunBattleEgg, DinoPackunBattleEggNrvWalk)) && getHost()->mFront.dot(away) < 0.0f) {
        setNerve(GET_NERVE(DinoPackunBattleEgg, DinoPackunBattleEggNrvTurn));
    }

    return true;
}

bool DinoPackunBattleEgg::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(DinoPackunBattleEgg, DinoPackunBattleEggNrvDamage))) {
        return mStateDamage->receiveOtherMsg(msg, pSender, pReceiver);
    }

    if (mStateDamage->isDamageMessage(msg)) {
        setNerve(GET_NERVE(DinoPackunBattleEgg, DinoPackunBattleEggNrvDamage));
        return true;
    }

    return false;
}

void DinoPackunBattleEgg::exeTurn() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "EggWalk");
        MR::startSound(getHost(), "SE_BV_D_PAKKUN_EGG_WALK");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_SLAVER");
    }

    getHost()->adjustTailRootPosition(cEggOutPosition, 1.0f);

    if (updateTurn(cTurnSteps, cTurnDegree)) {
        setNerve(GET_NERVE(DinoPackunBattleEgg, DinoPackunBattleEggNrvWalk));
    }
}

void DinoPackunBattleEgg::exeWalk() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "EggWalk");
    }

    getHost()->adjustTailRootPosition(cEggOutPosition, 1.0f);

    if (updateWalk(cWalkSteps, cWalkSpeed, cWalkFootPrintPeriod)) {
        setNerve(GET_NERVE(DinoPackunBattleEgg, DinoPackunBattleEggNrvTurn));
    }
}

void DinoPackunBattleEgg::exeDamage() {
    if (MR::updateActorState(this, mStateDamage)) {
        kill();
    }
}
