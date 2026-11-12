#include "Game/Screen/GameStageClearSequence.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/PowerStar.hpp"
#include "Game/Screen/SimpleLayout.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sClearDemoStopFrame = 10;
    const s32 sLayoutStartStepPowerStar = 247;
    const s32 sClearDemoWipeStepPowerStar = 325;
    const s32 sClearDemoWipeFramePowerStar = 80;
    const s32 sFanfareStartStepPowerStar = 15;
    const f32 sLayoutOffsetYPowerStar = 80.0f;
    static const s32 sLayoutStartStepGrandStar = NebulaFrameClock::stepsOfMilliseconds(8500);
    const s32 sClearDemoWipeStepGrandStar = 670;
    const s32 sFanfareStartStepGrandStar = 53;
    const f32 sLayoutOffsetYGrandStar = 20.0f;
    static const s32 sClearDemoWipeStepKoopaVs3 = NebulaFrameClock::stepsOfSeconds(14);
    const s32 sClearDemoWipeFrameKoopaVs3 = 60;
};  // namespace

namespace NrvGameStageClearSequence {
    NEW_NERVE(GameStageClearSequencePowerStarGetDemo, GameStageClearSequence, PowerStarGetDemo);
    NEW_NERVE(GameStageClearSequenceGrandStarGetDemo, GameStageClearSequence, GrandStarGetDemo);
};  // namespace NrvGameStageClearSequence

GameStageClearSequence::GameStageClearSequence()
    : LayoutActor("ステージクリアシーケンス", true), mEffectLayout(nullptr), mEffectFollowPos(0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< GameStageClearSequence >);
}

void GameStageClearSequence::init(const TableIter& /*rIter*/) {
    mEffectLayout = new SimpleEffectLayout("スター取得", "GetStar", 1, -1);
    mEffectLayout->initWithoutIter();

    MR::setFollowPos(&mEffectFollowPos, mEffectLayout, nullptr);
    MR::setFollowTypeAdd(mEffectLayout, nullptr);
    mEffectLayout->kill();

    initNerve(GET_NERVE(GameStageClearSequence, GameStageClearSequencePowerStarGetDemo));
    kill();
}

void GameStageClearSequence::startPowerStarGetDemo() {
    LayoutActor::appear();
    mEffectFollowPos.set(Vec2f(0.0f, ::sLayoutOffsetYPowerStar));
    MR::setTextBoxGameMessageRecursive(mEffectLayout, "GetStar", "GetStar_PowerStar");
    setNerve(GET_NERVE(GameStageClearSequence, GameStageClearSequencePowerStarGetDemo));
}

void GameStageClearSequence::startGrandStarGetDemo() {
    LayoutActor::appear();
    mEffectFollowPos.set(Vec2f(0.0f, ::sLayoutOffsetYGrandStar));
    MR::setTextBoxGameMessageRecursive(mEffectLayout, "GetStar", "GetStar_GrandStar");
    setNerve(GET_NERVE(GameStageClearSequence, GameStageClearSequenceGrandStarGetDemo));
}

void GameStageClearSequence::appearGetStarLayout() {
    mEffectLayout->appear();
    MR::startAnim(mEffectLayout, "GetStar", 0);
    MR::emitEffect(mEffectLayout, "GetStar");
}

void GameStageClearSequence::exePowerStarGetDemo() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(2);
        MR::setSoundVolumeSetting(1, 20);
        MR::stopScene(::sClearDemoStopFrame);
        MR::deactivateDefaultGameLayout();
        MR::endGalaxyCometEvent();
    }

    if (MR::isStep(this, ::sFanfareStartStepPowerStar)) {
        MR::startSubBGM("BGM_CLEAR", false);
    }

    if (MR::isStep(this, ::sLayoutStartStepPowerStar)) {
        appearGetStarLayout();
    }

    if (MR::isStep(this, ::sClearDemoWipeStepPowerStar)) {
        MR::closeWipeCircle(::sClearDemoWipeFramePowerStar);
    }

    bool isWipeActive = MR::isWipeActive();

    if (MR::isGreaterStep(this, ::sClearDemoWipeStepPowerStar) && !isWipeActive) {
        MR::requestChangeStageAfterStageClear();
        kill();
    }
}

void GameStageClearSequence::exeGrandStarGetDemo() {
    if (MR::isFirstStep(this)) {
        MR::stopStageBGM(2);
        MR::setSoundVolumeSetting(1, 20);
        MR::stopScene(::sClearDemoStopFrame);
        MR::deactivateDefaultGameLayout();
        MR::endGalaxyCometEvent();
    }

    if (MR::isStep(this, ::sFanfareStartStepGrandStar)) {
        if (PowerStar::isCurrentStageKoopaVs3()) {
            MR::startSubBGM("BGM_GRAND_STAR_GET_2", false);
        } else {
            MR::startSubBGM("BGM_GRAND_STAR_GET", false);
        }
    }

    if (MR::isStep(this, ::sLayoutStartStepGrandStar)) {
        appearGetStarLayout();
    }

    if (PowerStar::isCurrentStageKoopaVs3()) {
        if (MR::isStep(this, ::sClearDemoWipeStepKoopaVs3)) {
            MR::closeSystemWipeWhiteFade(::sClearDemoWipeFrameKoopaVs3);
        }

        bool isWipeActive = MR::isSystemWipeActive();

        if (MR::isGreaterStep(this, ::sClearDemoWipeStepKoopaVs3) && !isWipeActive) {
            MR::requestChangeStageAfterStageClear();
            kill();
        }
    } else {
        if (MR::isStep(this, ::sClearDemoWipeStepGrandStar)) {
            MR::closeWipeCircle();
        }

        bool isWipeActive = MR::isWipeActive();

        if (MR::isGreaterStep(this, ::sClearDemoWipeStepGrandStar) && !isWipeActive) {
            MR::requestChangeStageAfterStageClear();
            kill();
        }
    }
}
