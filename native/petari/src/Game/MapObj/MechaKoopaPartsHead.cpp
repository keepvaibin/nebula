#include "Game/MapObj/MechaKoopaPartsHead.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sStepForWhiteFadeOut = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sStepForWhiteFadeIn = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sStepToBreakFaceEffect = NebulaFrameClock::stepsOfSeconds(2);
    const s32 sStepToExplosionEffect = NebulaFrameClock::stepsOfSeconds(5);
    const char* const sDemoPartNameBreak = "壊れ開始";
    const char* const sDemoPartNameFadeOut = "フェードアウト";
    const char* const sDemoPartNameWhite = "白画面";
    const char* const sDemoPartNameFadeIn = "フェードイン";
};  // namespace

namespace NrvMechaKoopaPartsHead {
    NEW_NERVE(MechaKoopaPartsHeadNrvWait, MechaKoopaPartsHead, Wait);
    NEW_NERVE(MechaKoopaPartsHeadNrvDemoBreak, MechaKoopaPartsHead, DemoBreak);
    NEW_NERVE(MechaKoopaPartsHeadNrvDemoWhiteFadeOut, MechaKoopaPartsHead, DemoWhiteFadeOut);
    NEW_NERVE(MechaKoopaPartsHeadNrvDemoWhiteWait, MechaKoopaPartsHead, DemoWhiteWait);
    NEW_NERVE(MechaKoopaPartsHeadNrvDemoWhiteFadeIn, MechaKoopaPartsHead, DemoWhiteFadeIn);
    NEW_NERVE(MechaKoopaPartsHeadNrvDemoAppearStar, MechaKoopaPartsHead, DemoAppearStar);
};  // namespace NrvMechaKoopaPartsHead

MechaKoopaPartsHead::MechaKoopaPartsHead(const char* pName) : MapObjActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< MechaKoopaPartsHead >);
}

void MechaKoopaPartsHead::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo initInfo;
    initInfo.setupDefaultPos();
    initInfo.setupConnectToScene();
    initInfo.setupEffect(nullptr);
    initInfo.setupSound(6);
    initInfo.setupNerve(GET_NERVE(MechaKoopaPartsHead, MechaKoopaPartsHeadNrvWait));
    initialize(rIter, initInfo);
    MR::declarePowerStar(this);
    MR::registerDemoActionNerve(this, GET_NERVE(MechaKoopaPartsHead, MechaKoopaPartsHeadNrvDemoBreak), ::sDemoPartNameBreak);
    MR::registerDemoActionNerve(this, GET_NERVE(MechaKoopaPartsHead, MechaKoopaPartsHeadNrvDemoWhiteFadeOut), ::sDemoPartNameFadeOut);
    MR::registerDemoActionNerve(this, GET_NERVE(MechaKoopaPartsHead, MechaKoopaPartsHeadNrvDemoWhiteWait), ::sDemoPartNameWhite);
    MR::registerDemoActionNerve(this, GET_NERVE(MechaKoopaPartsHead, MechaKoopaPartsHeadNrvDemoWhiteFadeIn), ::sDemoPartNameFadeIn);

    if (MR::isValidSwitchA(this)) {
        MR::onSwitchA(this);
    }
}

void MechaKoopaPartsHead::exeWait() {
}

void MechaKoopaPartsHead::exeDemoBreak() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_BM_MECHA_KOOPA_DOWN");
        MR::moveVolumeStageBGM(0.0f, 30);
        MR::shakeCameraInfinity(this, 0.2f, 2.0f);
    }

    MR::tryRumblePadMiddle(this, PLAYER_1);
    MR::startLevelSound(this, "SE_BM_LV_MECHA_KOOPA_PRE_BREAK");

    if (MR::isStep(this, ::sStepToBreakFaceEffect)) {
        MR::emitEffect(this, "Explosion1");
        MR::emitEffect(this, "Explosion2");
        MR::emitEffect(this, "ExplosionEye1");
        MR::emitEffect(this, "ExplosionEye2");
        MR::startSound(this, "SE_OJ_M_KOOPA_EYE_BREAK");
    }

    if (MR::isStep(this, ::sStepToExplosionEffect)) {
        MR::emitEffect(this, "Explosion");
        MR::startSound(this, "SE_BM_MECHA_KOOPA_BREAK_FIRE_ST");
    }

    if (MR::isGreaterStep(this, ::sStepToExplosionEffect)) {
        MR::startLevelSound(this, "SE_BM_LV_MECHA_KOOPA_BREAK_FIRE");
    }
}

void MechaKoopaPartsHead::exeDemoWhiteFadeOut() {
    if (MR::isFirstStep(this)) {
        MR::stopShakingCamera(this);
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraStrong();
        MR::startSound(this, "SE_BM_MECHA_KOOPA_EXPLODE");
        MR::closeWipeWhiteFade(::sStepForWhiteFadeOut);
    }
}

void MechaKoopaPartsHead::exeDemoWhiteWait() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);

        if (MR::isValidSwitchA(this)) {
            MR::offSwitchA(this);
        }

        MR::setPlayerPos("爆破デモ後マリオ");
        MR::setPlayerStateWait();
    }
}

void MechaKoopaPartsHead::exeDemoWhiteFadeIn() {
    if (MR::isFirstStep(this)) {
        MR::openWipeWhiteFade(::sStepForWhiteFadeIn);
    }

    if (MR::isDemoPartLastStep(::sDemoPartNameFadeIn)) {
        setNerve(GET_NERVE(MechaKoopaPartsHead, MechaKoopaPartsHeadNrvDemoAppearStar));
    }
}

void MechaKoopaPartsHead::exeDemoAppearStar() {
    if (MR::isFirstStep(this)) {
        MR::startAfterBossBGM();
        MR::requestAppearPowerStar(this);
    }

    if (MR::isEndPowerStarAppearDemo(this)) {
        kill();
    }
}
