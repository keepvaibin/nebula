#include "Game/System/GameSystemFunction.hpp"
#include "Game/NameObj/NameObjRegister.hpp"
#include "Game/Scene/BootScene.hpp"
#include "Game/Screen/HomeButtonLayout.hpp"
#include "Game/Screen/ScreenPreserver.hpp"
#include "Game/System/AudSystemWrapper.hpp"
#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemErrorWatcher.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/System/GameSystemResetAndPowerProcess.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/GameSystemStationedArchiveLoader.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/System/InputRumble.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"

namespace {
    BootScene* getBootScene() {
        BootScene* pBootScene = static_cast< BootScene* >(SingletonHolder< GameSystem >::get()->mSceneController->mScene);

        if (pBootScene == nullptr) {
            return nullptr;
        }

        return pBootScene;
    }

    AudSystemWrapper* getAudSystemWrapper() {
        return SingletonHolder< GameSystem >::get()->mObjHolder->mAudioSystem;
    }

    SaveDataHandleSequence* getSaveDataHandleSequence() {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->mSaveDataHandleSequence;
    }
};  // namespace

namespace GameSystemFunction {
    void initAfterStationedResourceLoaded() {
        SingletonHolder< GameSystem >::get()->initAfterStationedResourceLoaded();
    }

    void setSceneNameObjHolderToNameObjRegister() {
        SingletonHolder< NameObjRegister >::get()->setCurrentHolder(SingletonHolder< GameSystem >::get()->mSceneController->mObjHolder);
    }

    bool isCreatedGameDataHolder() {
        return SingletonHolder< GameSystem >::get()->mSequenceDirector->isInitializedGameDataHolder();
    }

    bool isCreatedSystemWipe() {
        return SingletonHolder< GameSystem >::get()->mSystemWipeHolder != nullptr;
    }

    bool isDoneLoadSystemArchive() {
        return SingletonHolder< GameSystem >::get()->isDoneLoadSystemArchive();
    }

    bool tryToLoadSystemArchive() {
        bool isExecuteLoadSystemArchive = SingletonHolder< GameSystem >::get()->isExecuteLoadSystemArchive();

        if (!isExecuteLoadSystemArchive) {
            SingletonHolder< GameSystem >::get()->startToLoadSystemArchive();
        }

        return !isExecuteLoadSystemArchive;
    }

    void requestChangeArchivePlayer(bool isPlayerMario) {
        SingletonHolder< GameSystem >::get()->mStationedArchiveLoader->requestChangeArchivePlayer(isPlayerMario);
    }

    void waitEndChangeArchivePlayer() {
        SingletonHolder< GameSystem >::get()->mStationedArchiveLoader->waitDone();
    }

    void activateScreenPreserver() {
        SingletonHolder< GameSystem >::get()->mObjHolder->mScreenPreserver->activate();
    }

    void deactivateScreenPreserver() {
        SingletonHolder< GameSystem >::get()->mObjHolder->mScreenPreserver->deactivate();
    }

    bool isOccurredSystemWarning() {
        return SingletonHolder< GameSystem >::get()->mErrorWatcher->isWarning();
    }

    bool isResetProcessing() {
        return SingletonHolder< GameSystemResetAndPowerProcess >::get()->isActive();
    }

    void setResetOperationApplicationReset() {
        SingletonHolder< GameSystemResetAndPowerProcess >::get()->setResetOperationApplicationReset();
    }

    void setResetOperationQuit() {
        SingletonHolder< GameSystemResetAndPowerProcess >::get()->setResetOperationQuit();
    }

    void requestResetGameSystem(bool param1) {
        SingletonHolder< GameSystemResetAndPowerProcess >::get()->requestReset(param1);
    }

    void requestQuitGame(bool param1) {
        SingletonHolder< GameSystemResetAndPowerProcess >::get()->requestQuitGame(param1);
    }

    void forceToDeactivateHomeButtonLayout() {
        HomeButtonLayout* pHomeButtonLayout = SingletonHolder< GameSystem >::get()->mHomeButtonLayout;

        if (pHomeButtonLayout != nullptr) {
            pHomeButtonLayout->forceToDeactive();
        }
    }

    void resetCurrentScenarioNo() {
        SingletonHolder< GameSystem >::get()->mSceneController->resetCurrentScenarioNo();
    }

    bool isPrepareResetSaveDataHandleSequence() {
        if (::getSaveDataHandleSequence() != nullptr) {
            return ::getSaveDataHandleSequence()->isPreparedReset();
        }

        return true;
    }

    void restoreFromResetSaveDataHandleSequence() {
        if (::getSaveDataHandleSequence() != nullptr) {
            ::getSaveDataHandleSequence()->restoreFromReset();
        }
    }

    void prepareResetAudioSystem() {
        ::getAudSystemWrapper()->prepareReset();
    }

    void requestResetAudioSystem(bool param1) {
        ::getAudSystemWrapper()->requestReset(param1);
    }

    bool isDoneResetAudioSystem() {
        return ::getAudSystemWrapper()->isResetDone();
    }

    void resumeResetAudioSystem() {
        ::getAudSystemWrapper()->resumeReset();
    }

    void resetAllControllerRumble() {
        for (s32 chan = PLAYER_1; chan < PLAYER_1 + MR::getPlayerMaxCount(); chan++) {
            MR::getPlayerInput(chan)->_18->registInstance();
            MR::getPlayerInput(chan)->_18->stop();
        }
    }

    void setInputSleepTime(bool isLongAutoSleep) {
        MR::setInputSleepTime(isLongAutoSleep);
    }

    bool setPermissionToCheckInput(bool permission) {
        return SingletonHolder< GameSystem >::get()->mErrorWatcher->setPermissionUpdateInputStatus(permission);
    }

    void onPauseBeginAllRumble() {
        for (s32 chan = PLAYER_1; chan < PLAYER_1 + MR::getPlayerMaxCount(); chan++) {
            MR::getPlayerInput(chan)->_18->stop();
            MR::getPlayerInput(chan)->_1C->registInstance();
            MR::getPlayerInput(chan)->_1C->stop();
        }
    }

    void onPauseEndAllRumble() {
        for (s32 chan = PLAYER_1; chan < PLAYER_1 + MR::getPlayerMaxCount(); chan++) {
            MR::getPlayerInput(chan)->_18->registInstance();
            MR::getPlayerInput(chan)->_1C->stop();
        }
    }

    void onHomeButtonMenuBeginAllRumble() {
        for (s32 chan = PLAYER_1; chan < PLAYER_1 + MR::getPlayerMaxCount(); chan++) {
            MR::getPlayerInput(chan)->_18->pause();
            MR::getPlayerInput(chan)->_1C->registInstance();
            MR::getPlayerInput(chan)->_1C->stop();
        }
    }

    void onHomeButtonMenuCloseAllRumble() {
        for (s32 chan = PLAYER_1; chan < PLAYER_1 + MR::getPlayerMaxCount(); chan++) {
            MR::getPlayerInput(chan)->_18->registInstance();
            MR::getPlayerInput(chan)->_1C->stop();
        }
    }

    void onHomeButtonMenuEndAllRumble() {
        InputRumble* pRumble;

        for (s32 chan = PLAYER_1; chan < PLAYER_1 + MR::getPlayerMaxCount(); chan++) {
            pRumble = MR::getPlayerInput(chan)->_18;
            pRumble->_BC = 200;
            pRumble->stop();

            pRumble = MR::getPlayerInput(chan)->_1C;
            pRumble->_BC = 200;
            pRumble->stop();
        }
    }

    void prepareResetSystem() {
        SingletonHolder< GameSystem >::get()->mSceneController->prepareReset();
    }

    bool isPreparedFadeinSystem() {
        return SingletonHolder< GameSystem >::get()->mSceneController->isPreparedReset();
    }

    void restartSceneController() {
        SingletonHolder< GameSystem >::get()->mSceneController->restartGameAfterResetting();
    }

    bool isBeforeTitle() {
        return MR::isEqualSceneName("Boot") && ::getBootScene() != nullptr && ::getBootScene()->isBeforeTitle();
    }
};  // namespace GameSystemFunction
