#include "Game/System/GameSystemErrorWatcher.hpp"
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/BatteryInfo.hpp"
#include "Game/Screen/ErrorMessageWindow.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/TriggerChecker.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/NebulaInput.hpp"
#include <nebula_game_api.h>

#define INPUT_STATUS_OK 0
#define INPUT_STATUS_DISCONNECT 1
#define INPUT_STATUS_NO_BATTERY 2
#define INPUT_STATUS_NO_MOVE_STICK 3

namespace {
    const GameSystemErrorWatcher::Message cMessageUnplagged = {
        "CONT_01",
        ErrorMessageWindow::MessageType_Game,
    };
    const GameSystemErrorWatcher::Message cMessageDisconnect = {
        "ControllerLost",
        ErrorMessageWindow::MessageType_Nebula,
    };
    const GameSystemErrorWatcher::Message cMessageWiiRemoteLost = {
        "WiiRemoteLost",
        ErrorMessageWindow::MessageType_Nebula,
    };
    const GameSystemErrorWatcher::Message cMessageNoControls = {
        "ControlsMissing",
        ErrorMessageWindow::MessageType_Nebula,
    };
    const GameSystemErrorWatcher::Message cMessageNoBattery = {
        "ControllerBatteryOut",
        ErrorMessageWindow::MessageType_Nebula,
    };

    static const s32 sCounterMaxIgnoreCheckMoveStick = 90;
    static const s32 sCounterMaxDecideDisconnect = 120;
};  // namespace

namespace {
    NEW_NERVE(GameSystemErrorWatcherNoError, GameSystemErrorWatcher, NoError);
    NEW_NERVE(GameSystemErrorWatcherErrorWindowIn, GameSystemErrorWatcher, ErrorWindowIn);
    NEW_NERVE(GameSystemErrorWatcherErrorWindowDisplay, GameSystemErrorWatcher, ErrorWindowDisplay);
    NEW_NERVE(GameSystemErrorWatcherErrorWindowOut, GameSystemErrorWatcher, ErrorWindowOut);
};  // namespace

GameSystemErrorWatcher::GameSystemErrorWatcher()
    : NerveExecutor("ErrorWatcher"), mWindow(nullptr), mMessage(nullptr), mUnplaggedTexMap(nullptr), mBatteryLayout(nullptr),
      _1C(nullptr), mInputStatus(INPUT_STATUS_OK), mCounterIgnoreCheckMoveStick(0),
      mCounterDecideDisconnect(::sCounterMaxDecideDisconnect), mInputBattery(INPUT_BATTERY_MAX), mPermissionUpdateInputStatus(false) {
    mWindow = new ErrorMessageWindow();
    mWindow->initWithoutIter();

    _1C = new TriggerChecker();
    _1C->setInput(false);

    initNerve(GET_NERVE_ANON(GameSystemErrorWatcherNoError));
}

void GameSystemErrorWatcher::initAfterResourceLoaded() {
    mBatteryLayout = new BatteryLayout();
    mBatteryLayout->initWithoutIter();

    mUnplaggedTexMap = MR::createLytTexMap("ErrorMessageImage.arc", "FreeStyleUnplagged.bti");
}

void GameSystemErrorWatcher::movement() {
    mWindow->movement();
    mWindow->calcAnim();

    updateInputStatus();
    updateNerve();

    if (mPermissionUpdateInputStatus && mBatteryLayout != nullptr) {
        mBatteryLayout->movement();
        mBatteryLayout->calcAnim();
    }
}

void GameSystemErrorWatcher::draw() const {
    if (mPermissionUpdateInputStatus && mBatteryLayout != nullptr) {
        mBatteryLayout->draw();
    }

    mWindow->draw();
}

bool GameSystemErrorWatcher::isWarning() const {
    return !isNerve(GET_NERVE_ANON(GameSystemErrorWatcherNoError));
}

bool GameSystemErrorWatcher::setPermissionUpdateInputStatus(bool permission) {
    bool permissionPrev = mPermissionUpdateInputStatus;

    mPermissionUpdateInputStatus = permission;

    if (permission) {
        mCounterDecideDisconnect = 0;

        if (mBatteryLayout != nullptr && MR::isDead(mBatteryLayout)) {
            mBatteryLayout->appear();
        }
    } else {
        mInputStatus = INPUT_STATUS_OK;

        if (mBatteryLayout != nullptr && !MR::isDead(mBatteryLayout)) {
            mBatteryLayout->kill();
        }
    }

    return permissionPrev;
}

void GameSystemErrorWatcher::exeNoError() {
    const Message* pMessage = getProperMessageId();

    if (pMessage != nullptr) {
        mMessage = pMessage;

        setNerve(GET_NERVE_ANON(GameSystemErrorWatcherErrorWindowIn));

        if (AudWrap::getSystem() != nullptr) {
            AudWrap::getSystem()->pauseForErrorWindow();
        }
    }
}

void GameSystemErrorWatcher::exeErrorWindowIn() {
    if (MR::isFirstStep(this)) {
        mWindow->appearWithMessage(mMessage->mId, mMessage->mType, getProperErrorTexMap());
    }

    if (mWindow->isAnimEnd()) {
        setNerve(GET_NERVE_ANON(GameSystemErrorWatcherErrorWindowDisplay));
    }

    MR::requestStarPointerModeErrorWindow(this);
}

void GameSystemErrorWatcher::exeErrorWindowDisplay() {
    const Message* pMessage = getProperMessageId();

    if (pMessage != nullptr && pMessage != mMessage) {
        mMessage = pMessage;

        setNerve(GET_NERVE_ANON(GameSystemErrorWatcherErrorWindowIn));
    }

    if (pMessage == nullptr) {
        setNerve(GET_NERVE_ANON(GameSystemErrorWatcherErrorWindowOut));
        mWindow->disappear();

        mMessage = nullptr;
    }

    MR::requestStarPointerModeErrorWindow(this);
}

void GameSystemErrorWatcher::exeErrorWindowOut() {
    if (MR::isDead(mWindow)) {
        const Message* pMessage = getProperMessageId();

        if (pMessage != nullptr) {
            mMessage = pMessage;

            setNerve(GET_NERVE_ANON(GameSystemErrorWatcherErrorWindowIn));
        } else {
            if (AudWrap::getSystem() != nullptr) {
                AudWrap::getSystem()->resumeFromErrorWindow();
            }

            setNerve(GET_NERVE_ANON(GameSystemErrorWatcherNoError));
        }
    } else {
        MR::requestStarPointerModeErrorWindow(this);
    }
}

void GameSystemErrorWatcher::updateInputStatus() {
    bool isWaitDecideDisconnect;
    bool isOk;

    if (!mPermissionUpdateInputStatus) {
        mInputStatus = INPUT_STATUS_OK;
    } else {
        isOk = true;

        _1C->update(MR::getPlayerInput(PLAYER_1)->mIsConnected);

        if (MR::getPlayerInput(PLAYER_1)->mIsConnected) {
            mInputBattery = MR::getPlayerInput(PLAYER_1)->getBattery();
        }

        if (_1C->getLevel()) {
            mCounterDecideDisconnect = ::sCounterMaxDecideDisconnect;
        } else if (mCounterDecideDisconnect != 0) {
            mCounterDecideDisconnect--;
        }

        isWaitDecideDisconnect = mCounterDecideDisconnect != 0;

        if (_1C->getOnTrigger()) {
            mCounterIgnoreCheckMoveStick = ::sCounterMaxIgnoreCheckMoveStick;
        }

        if (mCounterIgnoreCheckMoveStick != 0) {
            mCounterIgnoreCheckMoveStick--;
        }

        if (mCounterIgnoreCheckMoveStick == 0) {
            if (_1C->getLevel() && !MR::getPlayerInput(PLAYER_1)->mHasMoveStick) {
                mInputStatus = INPUT_STATUS_NO_MOVE_STICK;
                isOk = false;
            }

            if (!isWaitDecideDisconnect) {
                if (mInputBattery <= INPUT_BATTERY_LOW) {
                    mInputStatus = INPUT_STATUS_NO_BATTERY;
                } else {
                    mInputStatus = INPUT_STATUS_DISCONNECT;
                }

                isOk = false;
            }

            if (isOk) {
                mInputStatus = INPUT_STATUS_OK;
            }
        }
    }
}

const GameSystemErrorWatcher::Message* GameSystemErrorWatcher::getProperMessageId() const {
    switch (mInputStatus) {
    case INPUT_STATUS_NO_MOVE_STICK:
        return &::cMessageUnplagged;
    case INPUT_STATUS_DISCONNECT:
        switch (NebulaHost::option(NEBULA_OPTION_INPUT_MODE)) {
        case NEBULA_INPUT_MODE_CONTROLLER:
            return &::cMessageDisconnect;
        case NEBULA_INPUT_MODE_WII_REMOTE:
            return &::cMessageWiiRemoteLost;
        default:
            return &::cMessageNoControls;
        }
    case INPUT_STATUS_NO_BATTERY:
        return &::cMessageNoBattery;
    default:
        return nullptr;
    }
}

const nw4r::lyt::TexMap* GameSystemErrorWatcher::getProperErrorTexMap() const {
    switch (mInputStatus) {
    case INPUT_STATUS_NO_MOVE_STICK:
        return mUnplaggedTexMap;
    default:
        return nullptr;
    }
}
