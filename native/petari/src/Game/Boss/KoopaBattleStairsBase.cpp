#include "Game/Boss/KoopaBattleStairsBase.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/Map/KoopaBattleMapStair.hpp"
#include "Game/Util/JointUtil.hpp"

namespace {
    const f32 cFireRadius = 90.0f;
}  // namespace

KoopaBattleStairsBase::KoopaBattleStairsBase(Koopa* pKoopa) : NerveExecutor("クッパ戦（階段の戦い）"), mKoopa(pKoopa), mStairsGroup() {
    mStairsGroup = new LiveActorGroup("階段保持", 128);
    mStairsGroup->initWithoutIter();
}

void KoopaBattleStairsBase::calcFireStartPos(Vec3f* pVec, const char* pDemoName) const {
    if (pDemoName != nullptr) {
        KoopaFunction::setKoopaPos(mKoopa, pDemoName);
    }

    MR::copyJointPos(mKoopa, "Tongue2", pVec);
}

void KoopaBattleStairsBase::update() {
    updateNerve();
}

void KoopaBattleStairsBase::end() {
    for (int idx = 0; idx < mStairsGroup->getObjNum(); idx++) {
        mStairsGroup->getActor(idx)->makeActorDead();
    }

    KoopaFunction::killKoopaFireStairsAll(mKoopa);
}

bool KoopaBattleStairsBase::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return KoopaFunction::tryKoopaReflectStarPiece(msg, pSender, pReceiver);
}

// The stair step at which Koopa must start the attack for the fire to reach the stair as it breaks.
s32 KoopaBattleStairsBase::calcFireAttackStep(KoopaBattleMapStair* pBattleMapStair, f32 fireSpeed, s32 emitStep, const Vec3f& rFireStartPos) const {
    Vec3f targetPos(0.0f, 0.0f, 0.0f);
    f32 dist = pBattleMapStair->calcAndSetTargetPos(&targetPos, rFireStartPos);

    const s32 fireTravelStep = static_cast< s32 >(dist / fireSpeed) - static_cast< s32 >(::cFireRadius / fireSpeed);

    return pBattleMapStair->mTimeToBreak - fireTravelStep - emitStep;
}
