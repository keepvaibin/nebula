#include "Game/NPC/RosettaDemoHeavensDoor.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/NPC/Rosetta.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_RosettaDemoHeavensDoor1_f1, "RosettaDemoHeavensDoor1.f1", 13.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_RosettaDemoHeavensDoor1_f2, "RosettaDemoHeavensDoor1.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_RosettaDemoHeavensDoor1_f3, "RosettaDemoHeavensDoor1.f3", 25.14f);
NEBULA_TUNABLE_NAMED(f32, cNum_RosettaDemoHeavensDoor1_f4, "RosettaDemoHeavensDoor1.f4", 6.16f);
NEBULA_TUNABLE_NAMED(f32, cNum_RosettaDemoHeavensDoor1_f5, "RosettaDemoHeavensDoor1.f5", 15064.593f);
NEBULA_TUNABLE_NAMED(f32, cNum_RosettaDemoHeavensDoor1_f6, "RosettaDemoHeavensDoor1.f6", 7917.67f);
NEBULA_TUNABLE_NAMED(f32, cNum_RosettaDemoHeavensDoor1_f7, "RosettaDemoHeavensDoor1.f7", 7541.112f);
NEBULA_TUNABLE_NAMED(f32, cNum_RosettaDemoHeavensDoor1_f8, "RosettaDemoHeavensDoor1.f8", 1500.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_RosettaDemoHeavensDoor1_exeWait_isNearPlayer_1, "RosettaDemoHeavensDoor1_exeWait.isNearPlayer.1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_RosettaDemoHeavensDoor2_exeWait_isNearPlayer_1, "RosettaDemoHeavensDoor2_exeWait.isNearPlayer.1", 400.0f);

namespace {
    NEBULA_TUNABLE(s32, sRosettaSwingVoiceFrame, 70);
    NEBULA_TUNABLE(s32, sRosettaSwingFrame, 80);
    NEBULA_TUNABLE(s32, sRosettaHideFrame, 10);
};  // namespace

namespace NrvRosettaDemoHeavensDoor1 {
    NEW_NERVE(RosettaDemoHeavensDoor1NrvWait, RosettaDemoHeavensDoor1, Wait);
    NEW_NERVE(RosettaDemoHeavensDoor1NrvFade, RosettaDemoHeavensDoor1, Fade);
    NEW_NERVE(RosettaDemoHeavensDoor1NrvDemo, RosettaDemoHeavensDoor1, Demo);
};  // namespace NrvRosettaDemoHeavensDoor1

RosettaDemoHeavensDoor1::RosettaDemoHeavensDoor1(Rosetta* pHost, const TableIter& rIter) : NerveExecutor("ロゼッタデモ実行者"), mHost(pHost) {
    DemoFunction::tryCreateDemoTalkAnimCtrlForActor(mHost, "DemoGetPower", "スピンゲット[デモ1]");
    initNerve(GET_NERVE(RosettaDemoHeavensDoor1, RosettaDemoHeavensDoor1NrvWait));

    mLightDomeModel = MR::createPartsModelNpc(mHost, "ライトドーム", "LightDome", nullptr);
    mLightDomeModel->makeActorAppeared();
    mLightDomeModel->initFixedPosition(Vec3f(0.0f, -cNum_RosettaDemoHeavensDoor1_f1, -cNum_RosettaDemoHeavensDoor1_f2), Vec3f(0.0f, 0.0f, 0.0f), "Center");
    MR::startBrk(mLightDomeModel, "LightDome");
    MR::startBck(mLightDomeModel, "Appear");

    if (MR::isDemoCast(mHost, nullptr)) {
        MR::tryRegisterDemoCast(mLightDomeModel, rIter);
    }

    DemoFunction::tryCreateDemoTalkAnimCtrlForActor(mLightDomeModel, "DemoGetPower", "スピンゲット[デモ1]");
    DemoFunction::registerDemoTalkMessageCtrl(mHost, mHost->mMsgCtrl);

    mLightHaloModel = MR::createPartsModelNpc(mHost, "ライト後光", "DomeHalo", nullptr);
    mLightHaloModel->initFixedPosition(Vec3f(0.0f, cNum_RosettaDemoHeavensDoor1_f3, -cNum_RosettaDemoHeavensDoor1_f4), Vec3f(0.0f, 0.0f, 0.0f), "Center");
    mLightHaloModel->makeActorAppeared();

    if (MR::isDemoCast(mHost, nullptr)) {
        MR::tryRegisterDemoCast(mLightHaloModel, rIter);
    }

    mLightHaloModel->mIsCalcOwnMtx = false;
    mLightHaloModel->mPosition.set(cNum_RosettaDemoHeavensDoor1_f5, -cNum_RosettaDemoHeavensDoor1_f6, cNum_RosettaDemoHeavensDoor1_f7);

    MR::needStageSwitchWriteA(mHost, rIter);
    MR::needStageSwitchWriteB(mHost, rIter);
    MR::registerDemoActionFunctor(mHost, MR::Functor(this, &RosettaDemoHeavensDoor1::preDemo), "高楼出現[デモ]");
    MR::registerDemoActionFunctor(mHost, MR::Functor(this, &RosettaDemoHeavensDoor1::pstDemo), "高楼出現[デモ後]");
    MR::registerDemoActionFunctor(mHost, MR::Functor(this, &RosettaDemoHeavensDoor1::fadeOut), "高楼出現[フェードアウト]");
    MR::registerDemoActionFunctor(mHost, MR::Functor(this, &RosettaDemoHeavensDoor1::fadeIn), "高楼出現[フェードイン]");
    MR::registerDemoActionFunctor(
        mHost, MR::Functor(this, &RosettaDemoHeavensDoor1::changeNerve< NrvRosettaDemoHeavensDoor1::RosettaDemoHeavensDoor1NrvDemo >),
        "スピンゲット[デモ1]");
    MR::invalidateShadowAll(mHost);
    MR::invalidateHitSensors(mHost);
    MR::setClippingTypeSphere(mHost, cNum_RosettaDemoHeavensDoor1_f8);
    mHost->startDemo(this);
    mHost->makeActorDead();
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

    if (MR::isNearPlayer(mHost->mMsgCtrl, cNum_RosettaDemoHeavensDoor1_exeWait_isNearPlayer_1) && !MR::isTimeKeepDemoActive()) {
        MR::offPlayerControl();
        MR::timeKeepDemoFadeOut();
        MR::startBrk(mLightHaloModel, "Disappear");
        setNerve(GET_NERVE(RosettaDemoHeavensDoor1, RosettaDemoHeavensDoor1NrvFade));
    }
}

void RosettaDemoHeavensDoor1::exeFade() {
    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        MR::onPlayerControl(true);
        MR::startTimeKeepDemoMarioPuppetable(mHost, "チコガイドデモ", "スピンゲット[デモ1]");
        mLightHaloModel->kill();
    }
}

void RosettaDemoHeavensDoor1::exeDemo() {
    if (MR::isDemoPartFirstStep("スピンゲット[デモ6]")) {
        MR::deleteEffect(mLightDomeModel, "Light");
    }

    if (MR::isDemoPartActive("スピンゲット[デモ3]")) {
        const s32 step = MR::getDemoPartStep("スピンゲット[デモ3]");

        if (step == ::sRosettaSwingVoiceFrame) {
            MR::startSound(mHost, "SE_SV_ROSETTA_SWING");
        }

        if (step == ::sRosettaSwingFrame) {
            MR::startSound(mHost, "SE_SM_ROSETTA_OP_SWING");
        }
    }

    if (MR::isDemoPartActive("スピンゲット[会話1]") || MR::isDemoPartActive("スピンゲット[会話2]") || MR::isDemoPartActive("スピンゲット[会話3]") ||
        MR::isDemoPartActive("スピンゲット[会話4]") || MR::isDemoPartActive("スピンゲット[デモ1]") || MR::isDemoPartActive("スピンゲット[デモ2]") ||
        MR::isDemoPartActive("スピンゲット[デモ3]") || MR::isDemoPartActive("スピンゲット[デモ4]") || MR::isDemoPartActive("スピンゲット[デモ5]")) {
        MR::startLevelSound(mHost, "SE_SM_LV_TICO_OP_WAIT");
    }

    if (MR::isDemoPartActive("スピンゲット[デモ6]")) {
        if (MR::getDemoPartStep("スピンゲット[デモ6]") < ::sRosettaHideFrame) {
            MR::startLevelSound(mHost, "SE_SM_LV_TICO_OP_WAIT");
        } else {
            MR::startLevelSound(mHost, "SE_SM_LV_ROSETTA_OP_HIDE");
        }
    }
}

namespace NrvRosettaDemoHeavensDoor2 {
    NEW_NERVE(RosettaDemoHeavensDoor2NrvWait, RosettaDemoHeavensDoor2, Wait);
    NEW_NERVE(RosettaDemoHeavensDoor2NrvDemo, RosettaDemoHeavensDoor2, Demo);
};  // namespace NrvRosettaDemoHeavensDoor2

RosettaDemoHeavensDoor2::RosettaDemoHeavensDoor2(Rosetta* pHost, const TableIter& rIter)
    : NerveExecutor("ロゼッタデモ実行者"), mDemoStarter(pHost), mHost(pHost) {
    DemoFunction::tryCreateDemoTalkAnimCtrlForScene(mHost, rIter, "DemoRedStar", "郷愁[開始]", 0, 0);
    DemoFunction::registerDemoTalkMessageCtrl(mHost, mHost->mMsgCtrl);
    MR::registerDemoActionFunctor(
        mHost, MR::Functor(this, &RosettaDemoHeavensDoor2::changeNerve< NrvRosettaDemoHeavensDoor2::RosettaDemoHeavensDoor2NrvDemo >), "郷愁[開始]");
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

void RosettaDemoHeavensDoor2::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(mHost, "WaitB");
    }

    if (MR::isNearPlayer(mHost, cNum_RosettaDemoHeavensDoor2_exeWait_isNearPlayer_1)) {
        mDemoStarter.start();
    }

    if (mDemoStarter.update()) {
        MR::tryStartTimeKeepDemoMarioPuppetable(mHost, "赤いスター", "郷愁[開始]");
        MR::onGameEventFlagRosettaTalkAboutTicoInTower();
    }
}
