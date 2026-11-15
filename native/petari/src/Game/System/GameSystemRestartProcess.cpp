#include "Game/System/GameSystemRestartProcess.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/Screen/NebulaLauncher.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/ValueControl.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <nebula_game_api.h>
#include "Game/System/LumaCore.hpp"

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    const s32 sFadeinoutFrame = 30;
    // The longest a diagnostics run's start stays black waiting for its first stage, in game steps (30 s): the run gives
    // up on a stage that does not load well before.
    const s32 cHoldDarkMostSteps = 30 * 60;

    // A diagnostics run is going (its panel is up).
    bool isRunGoing() {
        const NebulaHostServices* const pHost = nebula_host();
        NebulaBenchmarkState run;
        return pHost->benchmark_state != nullptr && pHost->benchmark_state(pHost->user, &run) != 0;
    }

    // A diagnostics run is loading the stage it shows next (not stopped by the player).
    bool isRunLoading() {
        const NebulaHostServices* const pHost = nebula_host();
        NebulaBenchmarkState run;
        return pHost->benchmark_state != nullptr && pHost->benchmark_state(pHost->user, &run) != 0 && run.showing == 0 &&
               run.stopping == 0;
    }
};  // namespace

namespace NrvGameSystemRestartProcess {
    NEW_NERVE(GameSystemRestartProcessIdle, GameSystemRestartProcess, Idle);
    NEW_NERVE(GameSystemRestartProcessFadeOut, GameSystemRestartProcess, FadeOut);
    NEW_NERVE(GameSystemRestartProcessWaitPrepareFadein, GameSystemRestartProcess, WaitPrepareFadein);
    NEW_NERVE(GameSystemRestartProcessFadein, GameSystemRestartProcess, Fadein);
};  // namespace NrvGameSystemRestartProcess

void GameSystemRestartProcess::init(const TableIter& /*rIter*/) {
    initNerve(GET_NERVE(GameSystemRestartProcess, GameSystemRestartProcessIdle));
    appear();
}

void GameSystemRestartProcess::draw() const {
    // While a diagnostics run is going its panel stays over the fade (drawHoldCover).
    if (!isActive() || isRunGoing()) {
        return;
    }

    MR::setupScreen2D();

    u8 alpha = MR::lerp(255, 0, mFadeinoutControl->getValue());
    MR::drawScreenColor(Color8(0, 0, 0, alpha));
}

void GameSystemRestartProcess::drawHoldCover() const {
    if (isActive() ? !isRunGoing() : !mHoldDark) {
        return;
    }

    MR::setupScreen2D();

    u8 alpha = MR::lerp(255, 0, mFadeinoutControl->getValue());
    MR::drawScreenColor(Color8(0, 0, 0, alpha));
}

bool GameSystemRestartProcess::isActive() const {
    return !isNerve(GET_NERVE(GameSystemRestartProcess, GameSystemRestartProcessIdle));
}

bool GameSystemRestartProcess::isScreenBlack() const {
    return (isActive() || mHoldDark) && mFadeinoutControl->mFrame == 0;
}

void GameSystemRestartProcess::setResetOperationApplicationReset() {
    mResetOperation = ResetOperation_ApplicationReset;
}

void GameSystemRestartProcess::setResetOperationQuit() {
    mResetOperation = ResetOperation_Quit;
}

void GameSystemRestartProcess::requestReset(bool isImmediate) {
    if (!isActive()) {
        beginFadeOut(isImmediate);
    }
}

void GameSystemRestartProcess::requestQuitGame(bool isImmediate) {
    if (!isActive()) {
        setResetOperationQuit();
        beginFadeOut(isImmediate);
    }
}

void GameSystemRestartProcess::beginFadeOut(bool isImmediate) {
    // A reset during the hold (the player stops the run) fades from the black it is at.
    mHoldDark = false;
    mHoldFading = false;
    mHoldSteps = 0;
    if (isImmediate) {
        mFadeinoutControl->setZero();
    } else {
        mFadeinoutControl->setDirToZero();
    }

    GameSystemFunction::activateScreenPreserver();
    setNerve(GET_NERVE(GameSystemRestartProcess, GameSystemRestartProcessFadeOut));
}

// Resets come from the system menu (requestReset, requestQuitGame), the file select's quit and the host (the benchmark
// in the running game, at its start and its end), past the boot.
void GameSystemRestartProcess::exeIdle() {
    // After a diagnostics run's reset: black until its first stage shows (or the run stops or ends, or the most steps),
    // then the fade in.
    if (mHoldDark) {
        if (!mHoldFading && (!isRunLoading() || ++mHoldSteps > cHoldDarkMostSteps)) {
            mHoldFading = true;
            mFadeinoutControl->setDirToOneResetFrame();
        } else if (mHoldFading && mFadeinoutControl->mFrame == mFadeinoutControl->mMaxFrame) {
            mHoldDark = false;
            mHoldFading = false;
            mHoldSteps = 0;
        }
    }
    if (!MR::isCurrentScene(SceneKind_Boot) && NebulaLauncher::takeReset()) {
        mResetOperation = ResetOperation_ApplicationReset;
        mReloadSaves = true;
        beginFadeOut(false);
    }
}

// The scene, the sound (stopped, then faded to silence) and the save sequence wind down while the picture fades.
void GameSystemRestartProcess::exeFadeOut() {
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

void GameSystemRestartProcess::reset() {
    GameSystemFunction::setPermissionToCheckInput(false);

    // New mods need a new run: the same fade and sound, then the game starts again with them. The host's reset keeps the
    // game it measures.
    if (mResetOperation != ResetOperation_ApplicationReset ||
        (!mReloadSaves && NebulaHost::option(NEBULA_OPTION_MODS_CHANGED) != 0)) {
        exitApplication();
    }

    setNerve(GET_NERVE(GameSystemRestartProcess, GameSystemRestartProcessWaitPrepareFadein));
}

void GameSystemRestartProcess::exeWaitPrepareFadein() {
    if (MR::isFirstStep(this)) {
        GameSystemFunction::deactivateScreenPreserver();
        GameSystemFunction::resumeResetAudioSystem();
        MR::forceOpenSystemWipeFade();
    }

    if (GameSystemFunction::isPreparedFadeinSystem()) {
        setNerve(GET_NERVE(GameSystemRestartProcess, GameSystemRestartProcessFadein));
    }
}

void GameSystemRestartProcess::exeFadein() {
    if (MR::isFirstStep(this)) {
        GameSystemFunction::restoreFromResetSaveDataHandleSequence();
        if (mReloadSaves) {
            GameSequenceFunction::startReloadSaveDataSequence();
        }
        // A diagnostics run's start stays black past the reset (exeIdle); any other fades in now.
        mHoldDark = mReloadSaves && isRunLoading();
        mHoldFading = false;
        mHoldSteps = 0;
        if (!mHoldDark) {
            mFadeinoutControl->setDirToOneResetFrame();
        }
    }

    // The host's reset: the title waits for the saves (the save sequence runs while the reset does).
    if (mReloadSaves) {
        if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
            return;
        }
        mReloadSaves = false;
        NebulaLauncher::resetDone();
    }

    // Held black: the game goes on (an active reset holds the scene) while the run travels to its stage.
    if (mHoldDark) {
        GameSystemFunction::restartSceneController();
        setNerve(GET_NERVE(GameSystemRestartProcess, GameSystemRestartProcessIdle));
        return;
    }

    if (mFadeinoutControl->mFrame == mFadeinoutControl->mMaxFrame) {
        GameSystemFunction::restartSceneController();
        setNerve(GET_NERVE(GameSystemRestartProcess, GameSystemRestartProcessIdle));
    }
}

void GameSystemRestartProcess::exitApplication() {
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

void GameSystemRestartProcess::control() {
    mFadeinoutControl->update();
}

GameSystemRestartProcess::GameSystemRestartProcess()
    : LayoutActor("リセット・電源", false), mFadeinoutControl(), mResetOperation(ResetOperation_Restart), mReloadSaves(false),
      mHoldDark(false), mHoldFading(false), mHoldSteps(0) {
    lumacore::setClass(this, lumacore::kClassId< GameSystemRestartProcess >);
    mFadeinoutControl = new ValueControl(::sFadeinoutFrame);
    mFadeinoutControl->setOne();
}
