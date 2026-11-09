#include "Game/Boss/KoopaDemoFallToPlanetVs3Lv2.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaBattleBase.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/Boss/KoopaPlanetShadow.hpp"
#include "Game/Boss/KoopaSwitchKeeper.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/KoopaBattleMapPlanet.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace NrvKoopaDemoFallToPlanetVs3Lv2 {
    NEW_NERVE(KoopaDemoFallToPlanetVs3Lv2NrvWaitFall, KoopaDemoFallToPlanetVs3Lv2, WaitFall);
    NEW_NERVE(KoopaDemoFallToPlanetVs3Lv2NrvDemoFall, KoopaDemoFallToPlanetVs3Lv2, DemoFall);
    NEW_NERVE(KoopaDemoFallToPlanetVs3Lv2NrvFallToLand, KoopaDemoFallToPlanetVs3Lv2, FallToLand);
    NEW_NERVE(KoopaDemoFallToPlanetVs3Lv2NrvLand, KoopaDemoFallToPlanetVs3Lv2, Land);
    NEW_NERVE(KoopaDemoFallToPlanetVs3Lv2NrvWaitPlayer, KoopaDemoFallToPlanetVs3Lv2, WaitPlayer);
};  // namespace NrvKoopaDemoFallToPlanetVs3Lv2

KoopaDemoFallToPlanetVs3Lv2::KoopaDemoFallToPlanetVs3Lv2(Koopa* pKoopa) : ActorStateBase< Koopa >("Demo[Ｌｖ２惑星まで落下]", pKoopa) {
}

void KoopaDemoFallToPlanetVs3Lv2::init() {
    KoopaFunction::initKoopaCamera(mHost, "惑星Ｌｖ２まで落下（後半）");
    KoopaFunction::initKoopaAnimCamera(mHost, "DemoKoopaFall");
    initNerve(GET_NERVE(KoopaDemoFallToPlanetVs3Lv2, KoopaDemoFallToPlanetVs3Lv2NrvWaitFall));
}

void KoopaDemoFallToPlanetVs3Lv2::appear() {
    mIsDead = false;

    KoopaFunction::getKoopaPlanetShadow(mHost)->kill();

    KoopaFunction::endFaceCtrl(mHost, -1);
}

void KoopaDemoFallToPlanetVs3Lv2::kill() {
    mIsDead = true;

    KoopaFunction::endKoopaCamera(mHost, "惑星Ｌｖ２まで落下（後半）", false);
}

void KoopaDemoFallToPlanetVs3Lv2::exeWaitFall() {
    if (KoopaFunction::tryStartKoopaAndMarioCameraDemo(mHost, "クッパＶｓ３惑星移動（Ｌｖ１からＬｖ２）", "DemoKoopaFall", "デモ中心")) {
        setNerve(GET_NERVE(KoopaDemoFallToPlanetVs3Lv2, KoopaDemoFallToPlanetVs3Lv2NrvDemoFall));
    }
}

void KoopaDemoFallToPlanetVs3Lv2::exeDemoFall() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(KoopaFunction::getKoopaPlanet(mHost), "Death");
    }

    if (KoopaFunction::tryEndKoopaCameraDemo(mHost, "クッパＶｓ３惑星移動（Ｌｖ１からＬｖ２）", "DemoKoopaFall")) {
        MR::offSwitchB(mHost);
        if (KoopaSwitchKeeper* pKeeper = KoopaFunction::getKoopaSwitchKeeper(mHost)) {
            MR::onSwitchA(pKeeper);
        }
        MR::overlayWithPreviousScreen(2);

        setNerve(GET_NERVE(KoopaDemoFallToPlanetVs3Lv2, KoopaDemoFallToPlanetVs3Lv2NrvFallToLand));
    }
}

void KoopaDemoFallToPlanetVs3Lv2::exeFallToLand() {
    if (MR::isFirstStep(this)) {
        KoopaFunction::setKoopaPos(mHost, "Ｌｖ２開始（クッパ）");
        MR::setPlayerPosAndWait("Ｌｖ２開始（マリオ）");

        KoopaFunction::startKoopaCamera(mHost, "惑星Ｌｖ２まで落下（後半）");
        MR::startAction(mHost, "JumpSoon");

        KoopaFunction::getKoopaPlanetShadow(mHost)->appear();
    }

    KoopaFunction::turnToPlayerFalling(mHost);

    MR::startLevelSound(mHost, "SE_BM_LV_KOOPA_FALL_TO_PLANET");

    if (MR::isBindedGround(mHost)) {
        MR::zeroVelocity(mHost);
        setNerve(GET_NERVE(KoopaDemoFallToPlanetVs3Lv2, KoopaDemoFallToPlanetVs3Lv2NrvLand));
    }
}

void KoopaDemoFallToPlanetVs3Lv2::exeLand() {
    if (MR::isFirstStep(this)) {
        KoopaFunction::startLandDemo(mHost);
    }

    if (MR::isActionEnd(mHost)) {
        setNerve(GET_NERVE(KoopaDemoFallToPlanetVs3Lv2, KoopaDemoFallToPlanetVs3Lv2NrvWaitPlayer));
    }
}

void KoopaDemoFallToPlanetVs3Lv2::exeWaitPlayer() {
    if (MR::isFirstStep(this)) {
        KoopaFunction::startFaceCtrl(mHost);
    }

    KoopaFunction::turnToPlayerWaiting(mHost);

    if (MR::isOnGroundPlayer()) {
        KoopaFunction::startFaceCtrl(mHost);
        kill();
    }
}
