#include "Game/Boss/KoopaBattleStairsVs3.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/KoopaBattleMapStair.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

namespace {
    const f32 cFireSpeed = 30.0f;
    const s32 cMeteorNum = 3;
    const char* const cMeteorDemoAnimName[cMeteorNum] = {
        "DemoKoopaBattleStairsVs3Start01",
        "DemoKoopaBattleStairsVs3Start02",
        "DemoKoopaBattleStairsVs3Start03",
    };

    ModelObj* getDemoMeteor(Koopa* pKoopa, s32 index) {
        switch (index) {
        case 0:
            return KoopaFunction::getKoopaDemoMeteor1(pKoopa);
        case 1:
            return KoopaFunction::getKoopaDemoMeteor2(pKoopa);
        default:
            return KoopaFunction::getKoopaDemoMeteor3(pKoopa);
        }
    }
}  // namespace

namespace NrvKoopaBattleStairsVs3 {
    NEW_NERVE(KoopaBattleStairsVs3NrvWaitDemo, KoopaBattleStairsVs3, WaitDemo);
    NEW_NERVE(KoopaBattleStairsVs3NrvDemo, KoopaBattleStairsVs3, Demo);
    NEW_NERVE(KoopaBattleStairsVs3NrvWait, KoopaBattleStairsVs3, Wait);
}  // namespace NrvKoopaBattleStairsVs3

KoopaBattleStairsVs3::KoopaBattleStairsVs3(Koopa* pKoopa) : KoopaBattleStairsBase(pKoopa), mFireStartPos(0.0f, 0.0f, 0.0f) {
    initNerve(GET_NERVE(KoopaBattleStairsVs3, KoopaBattleStairsVs3NrvWaitDemo));

    MR::findNamePos("クッパ階段戦の砲弾出現", &mFireStartPos, nullptr);

    KoopaFunction::initKoopaAnimCamera(mKoopa, "DemoKoopaBattleStairsVs3Start");
}

s32 KoopaBattleStairsVs3::registerStair(KoopaBattleMapStair* pBattleMapStair) {
    mStairsGroup->registerActor(pBattleMapStair);
    return calcFireAttackStep(pBattleMapStair, ::cFireSpeed, 0, mFireStartPos);
}

void KoopaBattleStairsVs3::exeWaitDemo() {
    if (!KoopaFunction::tryStartKoopaCameraDemo(mKoopa, "階段の戦い開始デモ", "DemoKoopaBattleStairsVs3Start", "デモ中心")) {
        return;
    }

    for (s32 i = 0; i < ::cMeteorNum; i++) {
        ::getDemoMeteor(mKoopa, i)->appear();
    }

    MR::requestMovementOn(KoopaFunction::getKoopaDemoPeach(mKoopa));
    MR::requestMovementOn(KoopaFunction::getKoopaDemoKoopaJr(mKoopa));
    MR::requestMovementOn(KoopaFunction::getKoopaDemoKoopaJrShip(mKoopa));
    for (s32 i = 0; i < ::cMeteorNum; i++) {
        MR::requestMovementOn(::getDemoMeteor(mKoopa, i));
    }

    MR::startAction(KoopaFunction::getKoopaDemoPeach(mKoopa), "DemoKoopaBattleStairsVs3Start");
    MR::startAction(KoopaFunction::getKoopaDemoKoopaJr(mKoopa), "DemoKoopaBattleStairsVs3Start");
    MR::startAction(KoopaFunction::getKoopaDemoKoopaJrShip(mKoopa), "DemoKoopaBattleStairsVs3Start");
    for (s32 i = 0; i < ::cMeteorNum; i++) {
        MR::startAction(::getDemoMeteor(mKoopa, i), ::cMeteorDemoAnimName[i]);
    }

    KoopaFunction::endFaceCtrl(mKoopa, -1);

    setNerve(GET_NERVE(KoopaBattleStairsVs3, KoopaBattleStairsVs3NrvDemo));
}

void KoopaBattleStairsVs3::exeDemo() {
    if (!KoopaFunction::tryEndKoopaCameraDemo(mKoopa, "階段の戦い開始デモ", "DemoKoopaBattleStairsVs3Start")) {
        return;
    }

    KoopaFunction::startFaceCtrl(mKoopa);

    MR::startAction(KoopaFunction::getKoopaDemoPeach(mKoopa), "DemoKoopaVs3Wait");
    MR::startAction(KoopaFunction::getKoopaDemoKoopaJr(mKoopa), "DemoKoopaVs3Wait");
    MR::startAction(KoopaFunction::getKoopaDemoKoopaJrShip(mKoopa), "DemoKoopaVs3Wait");

    for (s32 i = 0; i < ::cMeteorNum; i++) {
        ::getDemoMeteor(mKoopa, i)->kill();
    }

    setNerve(GET_NERVE(KoopaBattleStairsVs3, KoopaBattleStairsVs3NrvWait));
}

void KoopaBattleStairsVs3::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mKoopa, "Wait");
    }

    tryAttack();
}

// Each stair is fired at once, when its attack step comes or straight away if it has none.
void KoopaBattleStairsVs3::tryAttack() {
    for (s32 i = 0; i < mStairsGroup->getObjNum(); i++) {
        KoopaBattleMapStair* pBattleMapStair = static_cast< KoopaBattleMapStair* >(mStairsGroup->getActor(i));

        if (pBattleMapStair->isRequestAttackVs3()) {
            pBattleMapStair->mIsKoopaAttack = true;
            KoopaFunction::emitFireStairsToTarget(mKoopa, pBattleMapStair, mFireStartPos, true);
        }
    }
}
