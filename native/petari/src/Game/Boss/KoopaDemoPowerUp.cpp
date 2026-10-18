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
    static const s32 sStepToScreenBlur = 65;
    static const s32 sScreenBlurStep = 135;
    static const s32 sStepToScreenBlurFinal = 50;
    static const s32 sScreenBlurStepFinal = 140;
};  // namespace

namespace NrvKoopaDemoPowerUp {
    NEW_NERVE(KoopaDemoPowerUpNrvWaitDemo, KoopaDemoPowerUp, WaitDemo);
    NEW_NERVE(KoopaDemoPowerUpNrvDemo, KoopaDemoPowerUp, Demo);
};  // namespace NrvKoopaDemoPowerUp

KoopaDemoPowerUp::~KoopaDemoPowerUp() {
}

KoopaDemoPowerUp::KoopaDemoPowerUp(Koopa* pKoopa) : ActorStateBase< Koopa >("Demo[\203\160\203\217\201\133\203\101\203\142\203\166]" /* Demo[パワーアップ] */, pKoopa) {
}

void KoopaDemoPowerUp::init() {
    KoopaFunction::initKoopaCamera(getHost(), "\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202" /* パワーアップデモ */);
    KoopaFunction::initKoopaCamera(getHost(), "\215\305\217\111\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202" /* 最終パワーアップデモ */);

    MR::createCenterScreenBlur();
    initNerve(GET_NERVE(KoopaDemoPowerUp, KoopaDemoPowerUpNrvWaitDemo));
}

void KoopaDemoPowerUp::appear() {
    mIsDead = false;

    setNerve(GET_NERVE(KoopaDemoPowerUp, KoopaDemoPowerUpNrvWaitDemo));
}

void KoopaDemoPowerUp::kill() {
    mIsDead = true;

    KoopaFunction::endKoopaCamera(getHost(), "\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202" /* パワーアップデモ */, false);
    KoopaFunction::endKoopaCamera(getHost(), "\215\305\217\111\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202" /* 最終パワーアップデモ */, false);
}

void KoopaDemoPowerUp::exeWaitDemo() {
    if (!MR::tryStartDemoMarioPuppetable(getHost(), "\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202" /* パワーアップデモ */)) {
        return;
    }

    if (KoopaFunction::isKoopaVs1(getHost()) || KoopaFunction::isKoopaVs2(getHost())) {
        KoopaFunction::setKoopaPos(getHost(), "\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202\201\151\203\116\203\142\203\160\201\152" /* パワーアップデモ（クッパ） */);
        MR::setPlayerPosAndWait("\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202\201\151\203\175\203\212\203\111\201\152" /* パワーアップデモ（マリオ） */);

        if (KoopaFunction::isKoopaLv3(getHost())) {
            MR::startAction(getHost(), "DemoKoopaPowerUpFinal");
            KoopaFunction::startKoopaTargetCamera(getHost(), "\215\305\217\111\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202" /* 最終パワーアップデモ */);

            MR::onSwitchB(KoopaFunction::getKoopaPowerUpSwitch(getHost()));
        } else {
            MR::startAction(getHost(), "DemoKoopaPowerUp");
            KoopaFunction::startKoopaTargetCamera(getHost(), "\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202" /* パワーアップデモ */);

            MR::onSwitchA(KoopaFunction::getKoopaPowerUpSwitch(getHost()));
        }
    } else {
        KoopaFunction::setKoopaPos(getHost(), "\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202\202\153\202\226\202\122\201\151\203\116\203\142\203\160\201\152" /* パワーアップデモＬｖ３（クッパ） */);
        MR::setPlayerPosAndWait("\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202\202\153\202\226\202\122\201\151\203\175\203\212\203\111\201\152" /* パワーアップデモＬｖ３（マリオ） */);

        MR::startAction(getHost(), "DemoKoopaPowerUpFinal");
        KoopaFunction::startKoopaTargetCamera(getHost(), "\215\305\217\111\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202" /* 最終パワーアップデモ */);

        MR::onSwitchA(KoopaFunction::getKoopaPowerUpSwitch(getHost()));
    }

    MR::startBckPlayer("BattleWait");
    KoopaFunction::startRecoverKoopaArmor(getHost());
    KoopaFunction::startRecoverKoopaTailThorn(getHost());

    KoopaFunction::endFaceCtrl(getHost(), -1);

    setNerve(GET_NERVE(KoopaDemoPowerUp, KoopaDemoPowerUpNrvDemo));
}

void KoopaDemoPowerUp::exeDemo() {
    if (KoopaFunction::isKoopaLv3(mHost)) {
        if (MR::isStep(this, ::sStepToScreenBlurFinal)) {
            MR::startCenterScreenBlur(::sScreenBlurStepFinal, 15.0f, 80, 5, 30);
        }
    } else {
        if (MR::isStep(this, ::sStepToScreenBlur)) {
            MR::startCenterScreenBlur(::sScreenBlurStep, 15.0f, 80, 5, 30);
        }
    }

    if (!MR::isActionEnd(mHost)) {
        return;
    }

    if (KoopaFunction::isKoopaVs3(mHost) || KoopaFunction::isKoopaLv3(mHost)) {
        KoopaFunction::endKoopaCamera(mHost, "\215\305\217\111\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202" /* 最終パワーアップデモ */, false);
    } else {
        KoopaFunction::endKoopaCamera(mHost, "\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202" /* パワーアップデモ */, false);
    }

    MR::endDemo(mHost, "\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202" /* パワーアップデモ */);

    TVec3f gravity;
    gravity.negate(mHost->mGravity);
    MR::appearStarPieceToDirection(mHost, mHost->mPosition, gravity, 10, 50.0f, 60.0f, false);

    MR::startSound(mHost, "SE_OJ_STAR_PIECE_BURST");

    kill();
}
