#include "Game/NPC/RosettaDemoHeavensDoor.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/NPC/Rosetta.hpp"
#include "Game/NameObj/NameObjArchiveListCollector.hpp"
#include "Game/Util.hpp"

namespace {
    static const s32 sRosettaSwingVoiceFrame = 70;
    static const s32 sRosettaSwingFrame = 80;
    static const s32 sRosettaHideFrame = 10;
};  // namespace

namespace NrvRosettaDemoHeavensDoor1 {
    NEW_NERVE(RosettaDemoHeavensDoor1NrvWait, RosettaDemoHeavensDoor1, Wait);
    NEW_NERVE(RosettaDemoHeavensDoor1NrvFade, RosettaDemoHeavensDoor1, Fade);
    NEW_NERVE(RosettaDemoHeavensDoor1NrvDemo, RosettaDemoHeavensDoor1, Demo);
};  // namespace NrvRosettaDemoHeavensDoor1

RosettaDemoHeavensDoor1::RosettaDemoHeavensDoor1(Rosetta* pHost, const JMapInfoIter& rIter) : NerveExecutor("\203\215\203\133\203\142\203\136\203\146\203\202\216\300\215\163\216\322" /* ロゼッタデモ実行者 */), mHost(pHost) {
    DemoFunction::tryCreateDemoTalkAnimCtrlForActor(mHost, "DemoGetPower", "\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2021]" /* スピンゲット[デモ1] */);
    initNerve(GET_NERVE(RosettaDemoHeavensDoor1, RosettaDemoHeavensDoor1NrvWait));

    mLightDomeModel = MR::createPartsModelNpc(mHost, "\203\211\203\103\203\147\203\150\201\133\203\200" /* ライトドーム */, "LightDome", nullptr);
    mLightDomeModel->makeActorAppeared();
    mLightDomeModel->initFixedPosition(TVec3f(0.0f, -13.0f, -30.0f), TVec3f(0.0f, 0.0f, 0.0f), "Center");
    MR::startBrk(mLightDomeModel, "LightDome");
    MR::startBck(mLightDomeModel, "Appear");

    if (MR::isDemoCast(mHost, nullptr)) {
        MR::tryRegisterDemoCast(mLightDomeModel, rIter);
    }

    DemoFunction::tryCreateDemoTalkAnimCtrlForActor(mLightDomeModel, "DemoGetPower", "\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2021]" /* スピンゲット[デモ1] */);
    DemoFunction::registerDemoTalkMessageCtrl(mHost, mHost->mMsgCtrl);

    mLightHaloModel = MR::createPartsModelNpc(mHost, "\203\211\203\103\203\147\214\343\214\365" /* ライト後光 */, "DomeHalo", nullptr);
    mLightHaloModel->initFixedPosition(TVec3f(0.0f, 25.14f, -6.16f), TVec3f(0.0f, 0.0f, 0.0f), "Center");
    mLightHaloModel->makeActorAppeared();

    if (MR::isDemoCast(mHost, nullptr)) {
        MR::tryRegisterDemoCast(mLightHaloModel, rIter);
    }

    mLightHaloModel->mIsCalcOwnMtx = false;
    mLightHaloModel->mPosition.set(15064.593f, -7917.67f, 7541.112f);

    MR::needStageSwitchWriteA(mHost, rIter);
    MR::needStageSwitchWriteB(mHost, rIter);
    MR::registerDemoActionFunctor(mHost, MR::Functor(this, &RosettaDemoHeavensDoor1::preDemo), "\215\202\230\117\217\157\214\273[\203\146\203\202]" /* 高楼出現[デモ] */);
    MR::registerDemoActionFunctor(mHost, MR::Functor(this, &RosettaDemoHeavensDoor1::pstDemo), "\215\202\230\117\217\157\214\273[\203\146\203\202\214\343]" /* 高楼出現[デモ後] */);
    MR::registerDemoActionFunctor(mHost, MR::Functor(this, &RosettaDemoHeavensDoor1::fadeOut), "\215\202\230\117\217\157\214\273[\203\164\203\106\201\133\203\150\203\101\203\105\203\147]" /* 高楼出現[フェードアウト] */);
    MR::registerDemoActionFunctor(mHost, MR::Functor(this, &RosettaDemoHeavensDoor1::fadeIn), "\215\202\230\117\217\157\214\273[\203\164\203\106\201\133\203\150\203\103\203\223]" /* 高楼出現[フェードイン] */);
    MR::registerDemoActionFunctor(
        mHost, MR::Functor(this, &RosettaDemoHeavensDoor1::changeNerve< NrvRosettaDemoHeavensDoor1::RosettaDemoHeavensDoor1NrvDemo >),
        "\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2021]" /* スピンゲット[デモ1] */);
    MR::invalidateShadowAll(mHost);
    MR::invalidateHitSensors(mHost);
    MR::setClippingTypeSphere(mHost, 1500.0f);
    mHost->startDemo(this);
    mHost->makeActorDead();
}

void RosettaDemoHeavensDoor1::makeArchiveList(NameObjArchiveListCollector* pCollector, const JMapInfoIter& rIter) {
    pCollector->addArchive("LightDome");
    pCollector->addArchive("DomeHalo");
}

void RosettaDemoHeavensDoor1::preDemo() {
    MR::hidePlayer();
}

void RosettaDemoHeavensDoor1::pstDemo() {
    MR::startSound(mHost, "SE_OJ_ROSETTA_HALO_APPEAR");
}

void RosettaDemoHeavensDoor1::fadeOut() {
    MR::timeKeepDemoFadeOut();
}

void RosettaDemoHeavensDoor1::fadeIn() {
    mLightHaloModel->mIsCalcOwnMtx = true;

    MR::emitEffect(mLightDomeModel, "Light");
    MR::showPlayer();
    MR::timeKeepDemoFadeIn();
    MR::onSwitchB(mHost);
}

void RosettaDemoHeavensDoor1::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "DemoGetPowerStartWait");
    }

    if (MR::isNearPlayer(mHost->mMsgCtrl, 500.0f) && !MR::isTimeKeepDemoActive()) {
        MR::offPlayerControl();
        MR::timeKeepDemoFadeOut();
        MR::startBrk(mLightHaloModel, "Disappear");
        setNerve(GET_NERVE(RosettaDemoHeavensDoor1, RosettaDemoHeavensDoor1NrvFade));
    }
}

void RosettaDemoHeavensDoor1::exeFade() {
    if (MR::isStep(this, 90)) {
        MR::onPlayerControl(true);
        MR::startTimeKeepDemoMarioPuppetable(mHost, "\203\140\203\122\203\113\203\103\203\150\203\146\203\202" /* チコガイドデモ */, "\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2021]" /* スピンゲット[デモ1] */);
        mLightHaloModel->kill();
    }
}

void RosettaDemoHeavensDoor1::exeDemo() {
    if (MR::isDemoPartFirstStep("\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2026]" /* スピンゲット[デモ6] */)) {
        MR::deleteEffect(mLightDomeModel, "Light");
    }

    if (MR::isDemoPartActive("\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2023]" /* スピンゲット[デモ3] */)) {
        s32 step = MR::getDemoPartStep("\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2023]" /* スピンゲット[デモ3] */);

        if (step == ::sRosettaSwingVoiceFrame) {
            MR::startSound(mHost, "SE_SV_ROSETTA_SWING");
        }

        if (step == ::sRosettaSwingFrame) {
            MR::startSound(mHost, "SE_SM_ROSETTA_OP_SWING");
        }
    }

    if (MR::isDemoPartActive("\203\130\203\163\203\223\203\121\203\142\203\147[\211\357\230\1421]" /* スピンゲット[会話1] */) || MR::isDemoPartActive("\203\130\203\163\203\223\203\121\203\142\203\147[\211\357\230\1422]" /* スピンゲット[会話2] */) || MR::isDemoPartActive("\203\130\203\163\203\223\203\121\203\142\203\147[\211\357\230\1423]" /* スピンゲット[会話3] */) ||
        MR::isDemoPartActive("\203\130\203\163\203\223\203\121\203\142\203\147[\211\357\230\1424]" /* スピンゲット[会話4] */) || MR::isDemoPartActive("\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2021]" /* スピンゲット[デモ1] */) || MR::isDemoPartActive("\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2022]" /* スピンゲット[デモ2] */) ||
        MR::isDemoPartActive("\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2023]" /* スピンゲット[デモ3] */) || MR::isDemoPartActive("\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2024]" /* スピンゲット[デモ4] */) || MR::isDemoPartActive("\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2025]" /* スピンゲット[デモ5] */)) {
        MR::startLevelSound(mHost, "SE_SM_LV_TICO_OP_WAIT");
    }

    if (MR::isDemoPartActive("\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2026]" /* スピンゲット[デモ6] */)) {
        s32 step = MR::getDemoPartStep("\203\130\203\163\203\223\203\121\203\142\203\147[\203\146\203\2026]" /* スピンゲット[デモ6] */);

        if (step < ::sRosettaHideFrame) {
            MR::startLevelSound(mHost, "SE_SM_LV_TICO_OP_WAIT");
        }

        if (step >= ::sRosettaHideFrame) {
            MR::startLevelSound(mHost, "SE_SM_LV_ROSETTA_OP_HIDE");
        }
    }
}

namespace NrvRosettaDemoHeavensDoor2 {
    NEW_NERVE(RosettaDemoHeavensDoor2NrvWait, RosettaDemoHeavensDoor2, Wait);
    NEW_NERVE(RosettaDemoHeavensDoor2NrvDemo, RosettaDemoHeavensDoor2, Demo);
};  // namespace NrvRosettaDemoHeavensDoor2

RosettaDemoHeavensDoor2::RosettaDemoHeavensDoor2(Rosetta* pHost, const JMapInfoIter& rIter)
    : NerveExecutor("\203\215\203\133\203\142\203\136\203\146\203\202\216\300\215\163\216\322" /* ロゼッタデモ実行者 */), mDemoStarter(pHost), mHost(pHost) {
    DemoFunction::tryCreateDemoTalkAnimCtrlForScene(mHost, rIter, "DemoRedStar", "\213\275\217\104[\212\112\216\156]" /* 郷愁[開始] */, 0, 0);
    DemoFunction::registerDemoTalkMessageCtrl(mHost, mHost->mMsgCtrl);
    MR::registerDemoActionFunctor(
        mHost, MR::Functor(this, &RosettaDemoHeavensDoor2::changeNerve< NrvRosettaDemoHeavensDoor2::RosettaDemoHeavensDoor2NrvDemo >), "\213\275\217\104[\212\112\216\156]" /* 郷愁[開始] */);
    MR::needStageSwitchWriteA(mHost, rIter);

    if (MR::isOnGameEventFlagRosettaTalkAboutTicoInTower()) {
        mHost->makeActorDead();
    } else {
        MR::onSwitchA(mHost);
    }

    MR::invalidateShadowAll(mHost);
    MR::invalidateHitSensors(mHost);
    mHost->startDemo(this);
    initNerve(GET_NERVE(RosettaDemoHeavensDoor2, RosettaDemoHeavensDoor2NrvWait));
}

void RosettaDemoHeavensDoor2::makeArchiveList(NameObjArchiveListCollector* pCollector, const JMapInfoIter& rIter) {
}

void RosettaDemoHeavensDoor2::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "WaitB");
    }

    if (MR::isNearPlayer(mHost, 400.0f)) {
        mDemoStarter.start();
    }

    if (mDemoStarter.update()) {
        MR::tryStartTimeKeepDemoMarioPuppetable(mHost, "\220\324\202\242\203\130\203\136\201\133" /* 赤いスター */, "\213\275\217\104[\212\112\216\156]" /* 郷愁[開始] */);
        MR::onGameEventFlagRosettaTalkAboutTicoInTower();
    }
}
