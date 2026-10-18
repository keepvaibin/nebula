#include "Game/Boss/PoltaDemo.hpp"
#include "Game/Boss/Polta.hpp"
#include "Game/Boss/PoltaActionBase.hpp"
#include "Game/Boss/PoltaArm.hpp"
#include "Game/Boss/PoltaFunction.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"

namespace {
    static const s32 sBgmStartStep = 60;
    static const s32 sOpeningBlurStart = 300;
    static const s32 sOpeningBlurTime = 70;
    static const f32 sOpeningBlurOffset = 18.0f;
    static const u8 sOpeningBlurAlpha = 80;
    static const s32 sOpeningBlurFadeIn = 5;
    static const s32 sOpeningBlurFadeOut = 30;
    static const s32 sPowerUpBlurStart = 530;
    static const s32 sPowerUpBlurTime = 80;
    static const f32 sPowerUpBlurOffset = 20.0f;
    static const u8 sPowerUpBlurAlpha = 80;
    static const s32 sPowerUpBlurFadeIn = 5;
    static const s32 sPowerUpBlurFadeOut = 40;
};  // namespace

namespace NrvPoltaDemo {
    NEW_NERVE(PoltaDemoNrvTryStartDemo, PoltaDemo, TryStartDemo);
    NEW_NERVE(PoltaDemoNrvOpeningDemo, PoltaDemo, OpeningDemo);
    NEW_NERVE(PoltaDemoNrvPowerUpDemo, PoltaDemo, PowerUpDemo);
    NEW_NERVE(PoltaDemoNrvDownDemo, PoltaDemo, DownDemo);
};  // namespace NrvPoltaDemo

PoltaDemo::PoltaDemo(Polta* pPolta) : PoltaActionBase("\203\174\203\213\203\136\203\146\203\202" /* ポルタデモ */, pPolta), mCurDemoNerve(), mCurDemoName() {
}

void PoltaDemo::init() {
    initNerve(GET_NERVE(PoltaDemo, PoltaDemoNrvOpeningDemo));
}

void PoltaDemo::control() {
}

void PoltaDemo::startOpeningDemo() {
    startTryDemo("\203\174\203\213\203\136\212\112\216\156\203\146\203\202" /* ポルタ開始デモ */, GET_NERVE(PoltaDemo, PoltaDemoNrvOpeningDemo));
}

void PoltaDemo::startPowerUpDemo() {
    startTryDemo("\203\174\203\213\203\136\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202" /* ポルタパワーアップデモ */, GET_NERVE(PoltaDemo, PoltaDemoNrvPowerUpDemo));
}

void PoltaDemo::startDownDemo() {
    startTryDemo("\203\174\203\213\203\136\203\137\203\105\203\223\203\146\203\202" /* ポルタダウンデモ */, GET_NERVE(PoltaDemo, PoltaDemoNrvDownDemo));
}

void PoltaDemo::startDemoSetting() {
    MR::overlayWithPreviousScreen(2);
    setNerve(mCurDemoNerve);
    MR::resetPlayerEffect();
    mCurDemoName = nullptr;
    mCurDemoNerve = nullptr;
}

void PoltaDemo::startTryDemo(const char* pDemoName, const Nerve* pDemoNerve) {
    appear();
    mCurDemoNerve = pDemoNerve;
    mCurDemoName = pDemoName;
    if (MR::tryStartDemoMarioPuppetable(getHost(), pDemoName)) {
        startDemoSetting();
        return;
    }
    setNerve(GET_NERVE(PoltaDemo, PoltaDemoNrvTryStartDemo));
}

void PoltaDemo::exeTryStartDemo() {
    if (MR::tryStartDemoMarioPuppetable(getHost(), mCurDemoName)) {
        startDemoSetting();
    }
}

void PoltaDemo::exeOpeningDemo() {
    if (MR::isFirstStep(this)) {
        MR::showModel(getHost());
        PoltaFunction::killGroundRock(getHost());
        getHost()->setStartPose();
        MR::stopStageBGM(60);
        MR::startAnimCameraTargetSelf(getHost(), getHost()->mCameraInfo, "OpeningDemo", 0, 1.0f);
        MR::startAction(getHost(), "OpeningDemo");
        MR::startBckPlayer("BattleWait");
        PoltaFunction::onMovement(getHost());
        MR::setPlayerPos("\203\174\203\213\203\136\212\112\216\156\203\146\203\202\203\166\203\214\203\103\203\204\201\133\210\312\222\165" /* ポルタ開始デモプレイヤー位置 */);
        getHost()->mFormationModel->appear();
        MR::startAction(getHost()->mFormationModel, "OpeningDemo");
    }

    if (MR::isStep(this, ::sOpeningBlurStart)) {
        MR::startCenterScreenBlur(::sOpeningBlurTime, ::sOpeningBlurOffset, ::sOpeningBlurAlpha, ::sOpeningBlurFadeIn, ::sOpeningBlurFadeOut);
    }

    if (MR::isStep(this, 60)) {
        MR::startStageBGM("MBGM_BOSS_09_A", false);
    }

    if (MR::isActionEnd(getHost())) {
        MR::endAnimCamera(getHost(), getHost()->mCameraInfo, "OpeningDemo", -1, true);
        MR::endDemo(getHost(), "\203\174\203\213\203\136\212\112\216\156\203\146\203\202" /* ポルタ開始デモ */);
        getHost()->mFormationModel->kill();
        kill();
    }
}

void PoltaDemo::exePowerUpDemo() {
    if (MR::isFirstStep(this)) {
        PoltaFunction::killGroundRock(getHost());
        getHost()->setStartPose();
        MR::invalidateShadowAll(getHost()->mLeftArm);
        MR::invalidateShadowAll(getHost()->mRightArm);
        MR::stopStageBGM(60);
        PoltaFunction::onMovement(getHost());
        PoltaFunction::startArm(getHost());
        MR::startAnimCameraTargetSelf(getHost(), getHost()->mCameraInfo, "PowerUpDemo", 0, 1.0f);
        MR::startAction(getHost(), "PowerUpDemo");
        MR::startAction(PoltaFunction::getLeftArmActor(getHost()), "PowerUpDemo");
        MR::startAction(PoltaFunction::getRightArmActor(getHost()), "PowerUpDemo");
        MR::startBckPlayer("BattleWait");
        MR::setPlayerPos("\203\174\203\213\203\136\203\146\203\202\203\166\203\214\203\103\203\204\201\133\210\312\222\165" /* ポルタデモプレイヤー位置 */);
        getHost()->mFormationModel->appear();
        getHost()->mLeftArm->mFormationModel->appear();
        getHost()->mRightArm->mFormationModel->appear();
        MR::startAction(getHost()->mFormationModel, "PowerUpDemo");
        MR::startAction(getHost()->mLeftArm->mFormationModel, "PowerUpDemoArmL");
        MR::startAction(getHost()->mRightArm->mFormationModel, "PowerUpDemoArmR");
    }

    if (MR::isStep(this, ::sPowerUpBlurStart)) {
        MR::startCenterScreenBlur(::sPowerUpBlurTime, ::sPowerUpBlurOffset, ::sPowerUpBlurAlpha, ::sPowerUpBlurFadeIn, ::sPowerUpBlurFadeOut);
    }
    if (MR::isActionEnd(getHost())) {
        MR::endDemo(getHost(), "\203\174\203\213\203\136\203\160\203\217\201\133\203\101\203\142\203\166\203\146\203\202" /* ポルタパワーアップデモ */);
        MR::endAnimCamera(getHost(), getHost()->mCameraInfo, "PowerUpDemo", -1, true);
        MR::startStageBGM("MBGM_BOSS_09_B", false);
        MR::validateShadowAll(getHost()->mLeftArm);
        MR::validateShadowAll(getHost()->mRightArm);
        getHost()->mFormationModel->kill();
        getHost()->mLeftArm->mFormationModel->kill();
        getHost()->mRightArm->mFormationModel->kill();
        kill();
    }
}

void PoltaDemo::exeDownDemo() {
    if (MR::isFirstStep(this)) {
        PoltaFunction::killGroundRock(getHost());
        getHost()->setStartPose();
        MR::stopStageBGM(30);
        MR::startAnimCameraTargetSelf(getHost(), getHost()->mCameraInfo, "DownDemo", 0, 1.0f);
        MR::startAction(getHost(), "DownDemo");
        MR::startBckPlayer("BattleWait");
        PoltaFunction::onMovement(getHost());
        MR::setPlayerPos("\203\174\203\213\203\136\203\146\203\202\203\166\203\214\203\103\203\204\201\133\210\312\222\165" /* ポルタデモプレイヤー位置 */);
    }
    if (MR::isActionEnd(getHost())) {
        MR::endDemo(getHost(), "\203\174\203\213\203\136\203\137\203\105\203\223\203\146\203\202" /* ポルタダウンデモ */);
        MR::endAnimCamera(getHost(), getHost()->mCameraInfo, "DownDemo", -1, true);
        getHost()->kill();
        kill();
    }
}

PoltaDemo::~PoltaDemo() {
}
