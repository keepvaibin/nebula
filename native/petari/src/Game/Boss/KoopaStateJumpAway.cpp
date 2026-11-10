#include "Game/Boss/KoopaStateJumpAway.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeJump_f1, "exeJump.f1", 1500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeJump_f2, "exeJump.f2", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeJump_f3, "exeJump.f3", 2000.0f);

namespace {
    NEBULA_TUNABLE(f32, sJumpFrontSpeed, 20.0f);
    NEBULA_TUNABLE(f32, sJumpFlyUpSpeed, 35.0f);
    NEBULA_TUNABLE(f32, sJumpGravity, 1.0f);
    const s32 sLandStepVs3Lv3 = NebulaFrameClock::stepsOfMilliseconds(500);
};  // namespace

namespace NrvKoopaStateJumpAway {
    NEW_NERVE(KoopaStateJumpAwayNrvJumpStart, KoopaStateJumpAway, JumpStart);
    NEW_NERVE(KoopaStateJumpAwayNrvJump, KoopaStateJumpAway, Jump);
    NEW_NERVE(KoopaStateJumpAwayNrvLand, KoopaStateJumpAway, Land);
};  // namespace NrvKoopaStateJumpAway

KoopaStateJumpAway::KoopaStateJumpAway(Koopa* pKoopa) : ActorStateBase< Koopa >("State[ジャンプで離れる]", pKoopa) {
}

void KoopaStateJumpAway::init() {
    initNerve(GET_NERVE(KoopaStateJumpAway, KoopaStateJumpAwayNrvJumpStart));

    kill();
}

void KoopaStateJumpAway::appear() {
    mIsDead = false;

    setNerve(GET_NERVE(KoopaStateJumpAway, KoopaStateJumpAwayNrvJumpStart));
}

void KoopaStateJumpAway::exeJumpStart() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "JumpAwayStart");

        MR::zeroVelocity(mHost);
    }

    if (MR::isActionEnd(mHost)) {
        setNerve(GET_NERVE(KoopaStateJumpAway, KoopaStateJumpAwayNrvJump));
    }
}

void KoopaStateJumpAway::exeJump() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "JumpAway");

        MR::setVelocity(mHost, KoopaFunction::getKoopaFront(mHost) * -::sJumpFrontSpeed);
        MR::addVelocityJump(mHost, ::sJumpFlyUpSpeed);

        MR::startSound(mHost, "SE_BM_KOOPA_JUMP");
        MR::startSound(mHost, "SE_BV_KOOPA_JUMP");
    }

    MR::addVelocityToGravity(mHost, ::sJumpGravity);
    Vec3f newFront = mHost->mVelocity;
    MR::vecKillElement(newFront, mHost->mGravity, &newFront);

    if (!MR::isNearZero(newFront)) {
        MR::normalize(&newFront);

        KoopaFunction::getKoopaFrontPtr(mHost)->set(-newFront);
    }

    if (!MR::sendMsgEnemyAttackToBindedSensor(mHost, mHost->getSensor("Body")) && !MR::isFirstStep(this) &&
        mHost->mVelocity.dot(mHost->mGravity) >= 0.0f && MR::isBindedGround(mHost)) {
        MR::tryRumblePadAndCameraDistanceStrong(mHost, cNum_exeJump_f1, cNum_exeJump_f2, cNum_exeJump_f3);
        MR::startSound(mHost, "SE_BM_KOOPA_LAND");

        MR::zeroVelocity(mHost);

        setNerve(GET_NERVE(KoopaStateJumpAway, KoopaStateJumpAwayNrvLand));
    }
}

void KoopaStateJumpAway::exeLand() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "JumpAwayLand");
    }

    if ((KoopaFunction::isKoopaVs3(mHost) && KoopaFunction::isKoopaLv3(mHost) && MR::isGreaterStep(this, ::sLandStepVs3Lv3)) ||
        MR::isActionEnd(mHost)) {
        kill();
    }
}
