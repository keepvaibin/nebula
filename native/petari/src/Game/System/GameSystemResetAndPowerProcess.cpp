#include "Game/System/GameSystemResetAndPowerProcess.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/ValueControl.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    static const s32 sFadeinoutFrame = 30;
};  // namespace

namespace NrvGameSystemResetAndPowerProcess {
    NEW_NERVE(GameSystemResetAndPowerProcessIdle, GameSystemResetAndPowerProcess, Idle);
    NEW_NERVE(GameSystemResetAndPowerProcessFadeOut, GameSystemResetAndPowerProcess, FadeOut);
    NEW_NERVE(GameSystemResetAndPowerProcessWaitPrepareFadein, GameSystemResetAndPowerProcess, WaitPrepareFadein);
    NEW_NERVE(GameSystemResetAndPowerProcessFadein, GameSystemResetAndPowerProcess, Fadein);
};  // namespace NrvGameSystemResetAndPowerProcess

void GameSystemResetAndPowerProcess::init(const TableIter& rIter) {
    initNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessIdle));
    appear();
}

void GameSystemResetAndPowerProcess::draw() const {
    if (!isActive()) {
        return;
    }

    MR::setupScreen2D();

    u8 alpha = MR::lerp(255, 0, mFadeinoutControl->getValue());
    MR::drawScreenColor(Color8(0, 0, 0, alpha));
}

bool GameSystemResetAndPowerProcess::isActive() const {
    return !isNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessIdle));
}

void GameSystemResetAndPowerProcess::setResetOperationApplicationReset() {
    mResetOperation = ResetOperation_ApplicationReset;
}

void GameSystemResetAndPowerProcess::setResetOperationQuit() {
    mResetOperation = ResetOperation_Quit;
}

void GameSystemResetAndPowerProcess::requestReset(bool isImmediate) {
    if (!isActive()) {
        beginFadeOut(isImmediate);
    }
}

void GameSystemResetAndPowerProcess::requestQuitGame(bool isImmediate) {
    if (!isActive()) {
        setResetOperationQuit();
        beginFadeOut(isImmediate);
    }
}

void GameSystemResetAndPowerProcess::beginFadeOut(bool isImmediate) {
    if (isImmediate) {
        mFadeinoutControl->setZero();
    } else {
        mFadeinoutControl->setDirToZero();
    }

    GameSystemFunction::activateScreenPreserver();
    setNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessFadeOut));
}

// Resets come from the system menu (requestReset, requestQuitGame) and the file select's quit.
void GameSystemResetAndPowerProcess::exeIdle() {
}

// The scene, the sound (stopped, then faded to silence) and the save sequence wind down while the picture fades.
void GameSystemResetAndPowerProcess::exeFadeOut() {
    if (MR::isFirstStep(this)) {
        GameSystemFunction::prepareResetAudioSystem();
        GameSystemFunction::prepareResetSystem();
        GameSystemFunction::resetAllControllerRumble();
        GameSystemFunction::forceToDeactivateHomeButtonLayout();
        GameSystemFunction::requestResetAudioSystem(mResetOperation != ResetOperation_ApplicationReset);
    }

    if (mFadeinoutControl->mFrame == 0 && GameSystemFunction::isPrepareResetSaveDataHandleSequence() &&
        GameSystemFunction::isDoneResetAudioSystem()) {
        reset();
    }
}

void GameSystemResetAndPowerProcess::reset() {
    GameSystemFunction::setPermissionToCheckInput(false);

    // New mods need a new run: the same fade and sound, then the game starts again with them.
    if (mResetOperation != ResetOperation_ApplicationReset || NebulaHost::option(NEBULA_OPTION_MODS_CHANGED) != 0) {
        exitApplication();
    }

    setNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessWaitPrepareFadein));
}

void GameSystemResetAndPowerProcess::exeWaitPrepareFadein() {
    if (MR::isFirstStep(this)) {
        GameSystemFunction::deactivateScreenPreserver();
        GameSystemFunction::resumeResetAudioSystem();
        MR::forceOpenSystemWipeFade();
    }

    if (GameSystemFunction::isPreparedFadeinSystem()) {
        setNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessFadein));
    }
}

void GameSystemResetAndPowerProcess::exeFadein() {
    if (MR::isFirstStep(this)) {
        GameSystemFunction::restoreFromResetSaveDataHandleSequence();
    }

    if (MR::isFirstStep(this)) {
        mFadeinoutControl->setDirToOneResetFrame();
    }

    if (mFadeinoutControl->mFrame == mFadeinoutControl->mMaxFrame) {
        GameSystemFunction::restartSceneController();
        setNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessIdle));
    }
}

void GameSystemResetAndPowerProcess::exitApplication() {
    // The counts Nebula's achievements hold for the host (jumps, coins, play time), before the game ends.
    NebulaAchievements::flushStats();
    NebulaFrameClock::show(false);
    NebulaFrameClock::waitFrame();

    // The host closes the game for "leave the game", or starts it again; it does not return.
    const NebulaHostServices* const host = nebula_host();
    if (mResetOperation == ResetOperation_Quit) {
        host->app_exit(host->user);
    } else {
        host->app_restart(host->user);
    }
}

void GameSystemResetAndPowerProcess::control() {
    mFadeinoutControl->update();
}

GameSystemResetAndPowerProcess::GameSystemResetAndPowerProcess()
    : LayoutActor("リセット・電源", false), mFadeinoutControl(), mResetOperation(ResetOperation_Restart) {
    mFadeinoutControl = new ValueControl(::sFadeinoutFrame);
    mFadeinoutControl->setOne();
}
