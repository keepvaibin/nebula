#include "Game/Boss/KoopaDemoPowerUp.hpp"
#include "Game/Boss/Koopa.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/Boss/KoopaPowerUpSwitch.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    const s32 sStepToScreenBlur = 65;
    const s32 sScreenBlurStep = 135;
    const s32 sStepToScreenBlurFinal = 50;
    const s32 sScreenBlurStepFinal = 140;
};  // namespace

namespace NrvKoopaDemoPowerUp {
    NEW_NERVE(KoopaDemoPowerUpNrvWaitDemo, KoopaDemoPowerUp, WaitDemo);
    NEW_NERVE(KoopaDemoPowerUpNrvDemo, KoopaDemoPowerUp, Demo);
};  // namespace NrvKoopaDemoPowerUp

KoopaDemoPowerUp::KoopaDemoPowerUp(Koopa* pKoopa) : ActorStateBase< Koopa >("Demo[パワーアップ]", pKoopa) {
}

void KoopaDemoPowerUp::init() {
    KoopaFunction::initKoopaCamera(getHost(), "パワーアップデモ");
    KoopaFunction::initKoopaCamera(getHost(), "最終パワーアップデモ");

    MR::createCenterScreenBlur();
    initNerve(GET_NERVE(KoopaDemoPowerUp, KoopaDemoPowerUpNrvWaitDemo));
}

void KoopaDemoPowerUp::appear() {
    mIsDead = false;

    setNerve(GET_NERVE(KoopaDemoPowerUp, KoopaDemoPowerUpNrvWaitDemo));
}

void KoopaDemoPowerUp::kill() {
    mIsDead = true;

    KoopaFunction::endKoopaCamera(getHost(), "パワーアップデモ", false);
    KoopaFunction::endKoopaCamera(getHost(), "最終パワーアップデモ", false);
}

void KoopaDemoPowerUp::exeWaitDemo() {
    if (!MR::tryStartDemoMarioPuppetable(getHost(), "パワーアップデモ")) {
        return;
    }

    if (KoopaFunction::isKoopaVs1(getHost()) || KoopaFunction::isKoopaVs2(getHost())) {
        KoopaFunction::setKoopaPos(getHost(), "パワーアップデモ（クッパ）");
        MR::setPlayerPosAndWait("パワーアップデモ（マリオ）");

        if (KoopaFunction::isKoopaLv3(getHost())) {
            MR::startAction(getHost(), "DemoKoopaPowerUpFinal");
            KoopaFunction::startKoopaTargetCamera(getHost(), "最終パワーアップデモ");

            if (KoopaPowerUpSwitch* pSwitch = KoopaFunction::getKoopaPowerUpSwitch(getHost())) {
                MR::onSwitchB(pSwitch);
            }
        } else {
            MR::startAction(getHost(), "DemoKoopaPowerUp");
            KoopaFunction::startKoopaTargetCamera(getHost(), "パワーアップデモ");

            if (KoopaPowerUpSwitch* pSwitch = KoopaFunction::getKoopaPowerUpSwitch(getHost())) {
                MR::onSwitchA(pSwitch);
            }
        }
    } else {
        KoopaFunction::setKoopaPos(getHost(), "パワーアップデモＬｖ３（クッパ）");
        MR::setPlayerPosAndWait("パワーアップデモＬｖ３（マリオ）");

        MR::startAction(getHost(), "DemoKoopaPowerUpFinal");
        KoopaFunction::startKoopaTargetCamera(getHost(), "最終パワーアップデモ");

        if (KoopaPowerUpSwitch* pSwitch = KoopaFunction::getKoopaPowerUpSwitch(getHost())) {
            MR::onSwitchA(pSwitch);
        }
    }

    MR::startBckPlayer("BattleWait");
    KoopaFunction::startRecoverKoopaArmor(getHost());
    KoopaFunction::startRecoverKoopaTailThorn(getHost());

    KoopaFunction::endFaceCtrl(getHost(), -1);

    setNerve(GET_NERVE(KoopaDemoPowerUp, KoopaDemoPowerUpNrvDemo));
}

void KoopaDemoPowerUp::exeDemo() {
    const bool isFinal = KoopaFunction::isKoopaLv3(mHost);
    if (MR::isStep(this, isFinal ? ::sStepToScreenBlurFinal : ::sStepToScreenBlur)) {
        MR::startCenterScreenBlur(isFinal ? ::sScreenBlurStepFinal : ::sScreenBlurStep, 15.0f, 80, 5, 30);
    }

    if (!MR::isActionEnd(mHost)) {
        return;
    }

    if (KoopaFunction::isKoopaVs3(mHost) || KoopaFunction::isKoopaLv3(mHost)) {
        KoopaFunction::endKoopaCamera(mHost, "最終パワーアップデモ", false);
    } else {
        KoopaFunction::endKoopaCamera(mHost, "パワーアップデモ", false);
    }

    MR::endDemo(mHost, "パワーアップデモ");

    Vec3f gravity;
    gravity.negate(mHost->mGravity);
    MR::appearStarPieceToDirection(mHost, mHost->mPosition, gravity, 10, 50.0f, 60.0f, false);

    MR::startSound(mHost, "SE_OJ_STAR_PIECE_BURST");

    kill();
}
