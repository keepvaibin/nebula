#include "Game/System/GameSystemResetAndPowerProcess.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/MainLoopFramework.hpp"
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
    static const s32 sResetWaitFrame = 15;
};  // namespace

namespace NrvGameSystemResetAndPowerProcess {
    NEW_NERVE(GameSystemResetAndPowerProcessIdle, GameSystemResetAndPowerProcess, Idle);
    NEW_NERVE(GameSystemResetAndPowerProcessWaitResetPermitted, GameSystemResetAndPowerProcess, WaitResetPermitted);
    NEW_NERVE(GameSystemResetAndPowerProcessPrepareReset, GameSystemResetAndPowerProcess, PrepareReset);
    NEW_NERVE(GameSystemResetAndPowerProcessReset, GameSystemResetAndPowerProcess, Reset);
    NEW_NERVE(GameSystemResetAndPowerProcessWaitPrepareFadein, GameSystemResetAndPowerProcess, WaitPrepareFadein);
    NEW_NERVE(GameSystemResetAndPowerProcessFadein, GameSystemResetAndPowerProcess, Fadein);
};  // namespace NrvGameSystemResetAndPowerProcess

void GameSystemResetAndPowerProcess::init(const JMapInfoIter& rIter) {
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

void GameSystemResetAndPowerProcess::requestReset(bool param1) {
    if (tryPermitReset()) {
        _5E = param1;

        setNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessWaitResetPermitted));
    }
}

void GameSystemResetAndPowerProcess::requestQuitGame(bool param1) {
    if (tryPermitReset()) {
        _5E = param1;
        setResetOperationQuit();
        setNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessWaitResetPermitted));
    }
}

// Resets come from the HOME Menu (requestReset, requestQuitGame).
void GameSystemResetAndPowerProcess::exeIdle() {
}

void GameSystemResetAndPowerProcess::exeWaitResetPermitted() {
    if (GameSystemFunction::isPermitToResetAudioSystem() && GameSystemFunction::isPermitToResetSaveDataHandleSequence()) {
        if (_5E) {
            mFadeinoutControl->setZero();
        } else {
            mFadeinoutControl->setDirToZero();
        }

        _5E = false;

        GameSystemFunction::activateScreenPreserver();
        setNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessPrepareReset));
    }
}

void GameSystemResetAndPowerProcess::exePrepareReset() {
    if (MR::isFirstStep(this)) {
        GameSystemFunction::prepareResetAudioSystem();
        GameSystemFunction::prepareResetSystem();
        GameSystemFunction::resetAllControllerRumble();
        GameSystemFunction::forceToDeactivateHomeButtonLayout();
        GameSystemFunction::prepareResetSaveDataHandleSequence();
    }

    if (MR::isStep(this, 2)) {
        GameSystemFunction::requestResetAudioSystem(mResetOperation != ResetOperation_ApplicationReset);
    }

    bool b = mFadeinoutControl->mFrame == 0;

    if (b) {
        b = GameSystemFunction::isPrepareResetSaveDataHandleSequence();
    }

    if (b) {
        b = isResetAcceptAudio();
    }

    if (b) {
        setNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessReset));
    }
}

void GameSystemResetAndPowerProcess::exeReset() {
    GameSystemFunction::setPermissionToCheckInput(false);

    if (mResetOperation != ResetOperation_ApplicationReset) {
        exitApplication();
    }

    MR::setNerveAtStep(this, GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessWaitPrepareFadein), ::sResetWaitFrame);
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
    MainLoopFramework::prepareForExit();
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

bool GameSystemResetAndPowerProcess::tryPermitReset() {
    return !isActive();
}

bool GameSystemResetAndPowerProcess::isResetAcceptAudio() const {
    if (mResetOperation != ResetOperation_ApplicationReset && MR::isGreaterStep(this, 2) && GameSystemFunction::isDoneResetAudioSystem()) {
        return true;
    }

    return MR::isGreaterStep(this, 17);
}

void GameSystemResetAndPowerProcess::control() {
    mFadeinoutControl->update();
}

GameSystemResetAndPowerProcess::GameSystemResetAndPowerProcess()
    : LayoutActor("\203\212\203\132\203\142\203\147\201\105\223\144\214\271" /* リセット・電源 */, false), mFadeinoutControl(), mResetOperation(ResetOperation_Restart), _5C(true),
      _5E() {
    mFadeinoutControl = new ValueControl(::sFadeinoutFrame);
    mFadeinoutControl->setOne();
}

GameSystemResetAndPowerProcess::~GameSystemResetAndPowerProcess() {
}
