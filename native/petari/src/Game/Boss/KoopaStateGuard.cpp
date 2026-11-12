#include "Game/Boss/KoopaStateGuard.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaBattleBase.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    const MR::ActorMoveParam sGuardFallParam = {0.0f, 1.0f, 0.98f, 0.0f};
};  // namespace

namespace NrvKoopaStateGuard {
    NEW_NERVE(KoopaStateGuardNrvGuardFace, KoopaStateGuard, GuardFace);
    NEW_NERVE(KoopaStateGuardNrvGuardBody, KoopaStateGuard, GuardBody);
    NEW_NERVE(KoopaStateGuardNrvGuardTail, KoopaStateGuard, GuardTail);
};  // namespace NrvKoopaStateGuard

KoopaStateGuard::KoopaStateGuard(Koopa* pKoopa) : ActorStateBase< Koopa >("State[ガード攻撃]", pKoopa) {
}

void KoopaStateGuard::init() {
    initNerve(GET_NERVE(KoopaStateGuard, KoopaStateGuardNrvGuardFace));

    kill();
}

bool KoopaStateGuard::tryStart(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayer(pSender) || !MR::isMsgPlayerSpinAttack(msg)) {
        return false;
    }

    if (MR::isSensor(pReceiver, "Face")) {
        MR::sendMsgEnemyAttackFlip(pSender, pReceiver);

        setNerve(GET_NERVE(KoopaStateGuard, KoopaStateGuardNrvGuardFace));

        return true;
    }

    if (MR::isSensor(pReceiver, "GuardBody")) {
        MR::sendMsgEnemyAttackFlip(pSender, pReceiver);

        setNerve(GET_NERVE(KoopaStateGuard, KoopaStateGuardNrvGuardBody));

        return true;
    }

    if (MR::isSensor(pReceiver, "Tail") || MR::isSensor(pReceiver, "TailTop")) {
        MR::sendMsgEnemyAttackFlipMaximum(pSender, pReceiver);

        setNerve(GET_NERVE(KoopaStateGuard, KoopaStateGuardNrvGuardTail));

        return true;
    }

    return false;
}

void KoopaStateGuard::startGuard(const char* pActionName) {
    MR::startAction(mHost, pActionName);
    MR::startSound(mHost, "SE_BV_KOOPA_GUARD");

    MR::zeroVelocity(mHost);
}

void KoopaStateGuard::updateGuard() {
    MR::moveAndTurnToPlayer(mHost, KoopaFunction::getKoopaFrontPtr(mHost), ::sGuardFallParam);

    if (MR::isActionEnd(mHost)) {
        kill();
    }
}

void KoopaStateGuard::exeGuardFace() {
    if (MR::isFirstStep(this)) {
        startGuard("GuardFace");
    }

    updateGuard();
}

void KoopaStateGuard::exeGuardBody() {
    if (MR::isFirstStep(this)) {
        startGuard("GuardBody");
    }

    updateGuard();
}

void KoopaStateGuard::exeGuardTail() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "AttackTail");

        MR::zeroVelocity(mHost);
    }

    updateGuard();
}
