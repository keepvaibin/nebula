#include "Game/System/GameSystemResetAndPowerProcess.hpp"
#ifdef TARGET_PC
#include "Game/System/NebulaAchievements.hpp"
#endif
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
#include <JSystem/JUtility/JUTVideo.hpp>
#include "Game/System/NebulaFrameClock.hpp"
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    static const s32 sFadeinoutFrame = 30;
    static const s32 sResetWaitFrame = 15;
};  // namespace

namespace NrvGameSystemResetAndPowerProcess {
    NEW_NERVE(GameSystemResetAndPowerProcessPolling, GameSystemResetAndPowerProcess, Polling);
    NEW_NERVE(GameSystemResetAndPowerProcessWaitResetPermitted, GameSystemResetAndPowerProcess, WaitResetPermitted);
    NEW_NERVE(GameSystemResetAndPowerProcessPrepareReset, GameSystemResetAndPowerProcess, PrepareReset);
    NEW_NERVE(GameSystemResetAndPowerProcessReset, GameSystemResetAndPowerProcess, Reset);
    NEW_NERVE(GameSystemResetAndPowerProcessWaitPrepareFadein, GameSystemResetAndPowerProcess, WaitPrepareFadein);
    NEW_NERVE(GameSystemResetAndPowerProcessFadein, GameSystemResetAndPowerProcess, Fadein);
};  // namespace NrvGameSystemResetAndPowerProcess

void GameSystemResetAndPowerProcess::init(const JMapInfoIter& rIter) {
    initNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessPolling));
    appear();
}

void GameSystemResetAndPowerProcess::draw() const {
    if (!isActive()) {
        return;
    }

    J2DOrthoGraphSimple graph;
    graph.setPort();

    u8 alpha = MR::lerp(255, 0, mFadeinoutControl->getValue());
    graph.setColor(static_cast< const GXColor& >(GXColor(Color8(0, 0, 0, alpha))));

    f32 height = static_cast< s32 >(JUTVideo::getManager()->getRenderMode()->efbHeight);
    f32 width = MR::getScreenWidth();
    graph.fillBox(TBox2f(0.0f, 0.0f, width, height));
}

bool GameSystemResetAndPowerProcess::isActive() const NO_INLINE {
    return !isNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessPolling));
}

void GameSystemResetAndPowerProcess::setResetOperationApplicationReset() {
    mResetOperation = ResetOperation_ApplicationReset;
}

void GameSystemResetAndPowerProcess::setResetOperationReturnToMenu() {
    mResetOperation = ResetOperation_ReturnToMenu;
}

void GameSystemResetAndPowerProcess::requestReset(bool param1) {
    if (tryPermitReset()) {
        _5E = param1;

        setNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessWaitResetPermitted));
    }
}

void GameSystemResetAndPowerProcess::requestGoWiiMenu(bool param1) {
    if (tryPermitReset()) {
        _5E = param1;
        setResetOperationReturnToMenu();
        setNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessWaitResetPermitted));
    }
}

void GameSystemResetAndPowerProcess::notifyCheckDiskResult(bool param1) {
    _5C = true;

    if (param1) {
        return;
    }

    if (mResetOperation == ResetOperation_ApplicationReset) {
        setResetOperationReturnToMenu();
    }
}

// The console's power and reset buttons started a reset from here. A PC has neither: resets come from the HOME Menu
// (requestReset, requestGoWiiMenu).
void GameSystemResetAndPowerProcess::exePolling() {
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
    if (MR::isFirstStep(this)) {
        _5C = false;

        // The console checked the disc was still in (its answer came before the check returned); the game's files are.
        notifyCheckDiskResult(true);
    }

    if (_5C) {
        GameSystemFunction::setPermissionToCheckWiiRemoteConnectAndScreenDimming(false);

        if (mResetOperation != ResetOperation_ApplicationReset) {
            exitApplication();
        }
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
        GameSystemFunction::restartControllerLeaveWatcher();
        GameSystemFunction::restartSceneController();
        setNerve(GET_NERVE(GameSystemResetAndPowerProcess, GameSystemResetAndPowerProcessPolling));
    }
}

void GameSystemResetAndPowerProcess::exitApplication() {
#ifdef TARGET_PC
    // The counts Nebula's achievements hold for the host (jumps, coins, play time), before the game ends.
    NebulaAchievements::flushStats();
#endif
    MainLoopFramework::setForOSResetSystem();
    NebulaFrameClock::show(false);
    NebulaFrameClock::waitFrame();

    // The host starts the game again, or closes it for "leave the game" (the Wii Menu); it does not return.
    const NebulaHostServices* const host = nebula_host();
    host->power(host->user, mResetOperation == ResetOperation_ReturnToMenu ? NEBULA_POWER_MENU : NEBULA_POWER_RESTART);
}

bool GameSystemResetAndPowerProcess::tryPermitReset() NO_INLINE {
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
