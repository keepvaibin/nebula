#include "Game/Boss/DinoPackunBattleEggVs2.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/DinoPackun.hpp"
#include "Game/Boss/DinoPackunStateDamage.hpp"
#include "Game/Boss/DinoPackunStateFire.hpp"
#include "Game/Boss/DinoPackunTail.hpp"
#include "Game/Boss/DinoPackunTrackFire.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    // Where the tail leaves the egg, in the boss's frame.
    const Vec3f cEggOutPosition(0.0f, 60.0f, -320.0f);
    const s32 cTurnSteps = NebulaFrameClock::stepsOfMilliseconds(500);
    const f32 cTurnDegree = 1.5f;
    const s32 cWalkSteps = NebulaFrameClock::stepsOfMilliseconds(12500);
    const f32 cWalkSpeed = 0.9f;
    const s32 cWalkFootPrintPeriod = 40;
    const f32 cWalkTailBendRate = 1.5f;
    // The walk weaves, turning the other way every period.
    const s32 cWeavePeriod = 100;
    const f32 cWeaveDegree = 0.75f;
    // Each cycle the tail burns, cools, then warns before burning again. All three are plain step counts, so the
    // sign window is the cycle's last 20 steps at any step rate.
    const s32 cFireCycle = 200;
    const s32 cFireSteps = 80;
    const s32 cFireSignStep = 180;
};  // namespace

namespace NrvDinoPackunBattleEggVs2 {
    NEW_NERVE(DinoPackunBattleEggVs2NrvTurn, DinoPackunBattleEggVs2, Turn);
    NEW_NERVE(DinoPackunBattleEggVs2NrvWalk, DinoPackunBattleEggVs2, Walk);
    NEW_NERVE(DinoPackunBattleEggVs2NrvDamage, DinoPackunBattleEggVs2, Damage);
};  // namespace NrvDinoPackunBattleEggVs2

// The second fight's opening: inside the egg with a burning tail; the egg burns the player and hits other actors.
DinoPackunBattleEggVs2::DinoPackunBattleEggVs2(DinoPackun* pHost)
    : DinoPackunAction("2回戦卵バトル", pHost), mStateDamage(nullptr), mTrackFireHolder(nullptr), mStateFire(nullptr) {
}

void DinoPackunBattleEggVs2::setTrackFire(DinoPackunTrackFireHolder* pHolder) {
    mTrackFireHolder = pHolder;
}

void DinoPackunBattleEggVs2::init() {
    initNerve(GET_NERVE(DinoPackunBattleEggVs2, DinoPackunBattleEggVs2NrvTurn));
    mStateDamage = new DinoPackunStateDamage(getHost());
    mStateDamage->setDamageEgg();
    mStateFire = new DinoPackunStateFire(getHost());
    mStateFire->init();
}

void DinoPackunBattleEggVs2::appear() {
    mIsDead = false;
    getHost()->mTail->lockNodePosition(1);
    mStateFire->requestFireDirect();
    setNerve(GET_NERVE(DinoPackunBattleEggVs2, DinoPackunBattleEggVs2NrvTurn));
}

void DinoPackunBattleEggVs2::control() {
    mStateFire->update();
}

// Pushes whatever the attack did not take.
void DinoPackunBattleEggVs2::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!getHost()->isSensorEgg(pSender)) {
        return;
    }

    if (MR::isSensorPlayer(pReceiver)) {
        if (!MR::sendMsgEnemyAttackFire(pReceiver, pSender)) {
            MR::sendMsgPush(pReceiver, pSender);
        }
    } else if (!MR::sendMsgEnemyAttack(pReceiver, pSender)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

// The egg takes reflected star pieces.
bool DinoPackunBattleEggVs2::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    return getHost()->isSensorEgg(pReceiver) && MR::isMsgStarPieceReflect(msg);
}

bool DinoPackunBattleEggVs2::receiveMsgPush(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    return getHost()->isSensorEgg(pReceiver);
}

// A damage message puts out the fire trail; the tail cools once the damage state takes a message.
bool DinoPackunBattleEggVs2::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(DinoPackunBattleEggVs2, DinoPackunBattleEggVs2NrvDamage))) {
        if (mStateDamage->receiveOtherMsg(msg, pSender, pReceiver)) {
            mStateFire->requestCool();
            return true;
        }

        return false;
    }

    if (mStateDamage->isDamageMessage(msg)) {
        mTrackFireHolder->killAll();
        setNerve(GET_NERVE(DinoPackunBattleEggVs2, DinoPackunBattleEggVs2NrvDamage));
        return true;
    }

    return false;
}

void DinoPackunBattleEggVs2::exeTurn() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "EggWalk");
        MR::startSound(getHost(), "SE_BV_D_PAKKUN_EGG_WALK");
        MR::startSound(getHost(), "SE_BM_D_PAKKUN_LAVER");
    }

    getHost()->adjustTailRootPosition(cEggOutPosition, 1.0f);

    if (updateTurn(cTurnSteps, cTurnDegree)) {
        setNerve(GET_NERVE(DinoPackunBattleEggVs2, DinoPackunBattleEggVs2NrvWalk));
    }
}

void DinoPackunBattleEggVs2::exeWalk() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "EggWalk");
        getHost()->mTail->mBendRate = cWalkTailBendRate;
    }

    getHost()->adjustTailRootPosition(cEggOutPosition, 1.0f);

    s32 step = getNerveStep();
    f32 weaveSide = (step / cWeavePeriod) % 2 != 0 ? -1.0f : 1.0f;
    MR::rotateDirectionGravityDegree(getHost(), &getHost()->mFront, (cWeaveDegree * weaveSide));

    s32 cycleStep = step % cFireCycle;
    if (cycleStep < cFireSteps) {
        mStateFire->requestFire();
    } else if (cycleStep >= cFireSignStep) {
        mStateFire->requestFireSign();
    } else {
        mStateFire->requestCool();
    }

    if (updateWalk(cWalkSteps, cWalkSpeed, cWalkFootPrintPeriod)) {
        setNerve(GET_NERVE(DinoPackunBattleEggVs2, DinoPackunBattleEggVs2NrvTurn));
    }
}

void DinoPackunBattleEggVs2::exeDamage() {
    if (MR::isFirstStep(this)) {
        getHost()->mTail->mBendRate = 1.0f;
    }

    if (MR::updateActorState(this, mStateDamage)) {
        kill();
    }
}
