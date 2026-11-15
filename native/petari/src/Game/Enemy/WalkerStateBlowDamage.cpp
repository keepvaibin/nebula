#include "Game/Enemy/WalkerStateBlowDamage.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"

namespace {
    const f32 sAirFric = 0.99f;
    const f32 sAirGravityAccel = 1.0f;
    const f32 sDamageTurnLimit = 15.0f;
    const s32 sDamageLandTime = NebulaFrameClock::stepsOfMilliseconds(500);
};  // namespace

namespace NrvWalkerStateBlowDamage {
    NEW_NERVE(WalkerStateBlowDamageNrvBlow, WalkerStateBlowDamage, Blow);
    NEW_NERVE(WalkerStateBlowDamageNrvBlowLand, WalkerStateBlowDamage, BlowLand);
};  // namespace NrvWalkerStateBlowDamage

WalkerStateBlowDamage::WalkerStateBlowDamage(LiveActor* pHost, Vec3f* pDirection)
    : ActorStateBase< LiveActor >("吹き飛びダメージ状態", pHost), mDirection(pDirection) {
    initNerve(GET_NERVE(WalkerStateBlowDamage, WalkerStateBlowDamageNrvBlow));
}

void WalkerStateBlowDamage::appear() {
    mIsDead = false;
    setNerve(GET_NERVE(WalkerStateBlowDamage, WalkerStateBlowDamageNrvBlow));
}

void WalkerStateBlowDamage::exeBlow() {
    if (MR::isFirstStep(this)) {
        MR::startAction(getHost(), "Damage");
    }
    MR::attenuateVelocity(getHost(), ::sAirFric);
    MR::addVelocityToGravity(getHost(), ::sAirGravityAccel);
    MR::turnDirectionDegree(getHost(), mDirection, -getHost()->mVelocity, ::sDamageTurnLimit);

    if (MR::isGreaterStep(this, 5) && MR::isBindedGround(getHost())) {
        MR::startAction(getHost(), "DamageLand");
        MR::zeroVelocity(getHost());
        setNerve(GET_NERVE(WalkerStateBlowDamage, WalkerStateBlowDamageNrvBlowLand));
    }
}

void WalkerStateBlowDamage::exeBlowLand() {
    if (MR::isGreaterStep(this, ::sDamageLandTime)) {
        kill();
    }
}
