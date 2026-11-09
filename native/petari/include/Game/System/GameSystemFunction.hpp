#pragma once

namespace GameSystemFunction {
    void setSceneNameObjHolderToNameObjRegister();
    bool isCreatedGameDataHolder();
    bool isCreatedSystemWipe();
    bool isDoneLoadSystemArchive();
    bool tryToLoadSystemArchive();
    void activateScreenPreserver();
    void deactivateScreenPreserver();
    bool isOccurredSystemWarning();
    // True for the one frame the scene goes on after the HOME Menu, a warning, the skip's question or the settings menu held it.
    bool isSceneResumed();
    bool isResetProcessing();
    void setResetOperationApplicationReset();
    void setResetOperationQuit();
    void requestResetGameSystem(bool);
    void requestQuitGame(bool);
    void forceToDeactivateHomeButtonLayout();
    void resetCurrentScenarioNo();
    bool isPrepareResetSaveDataHandleSequence();
    void restoreFromResetSaveDataHandleSequence();
    void prepareResetAudioSystem();
    void requestResetAudioSystem(bool);
    bool isDoneResetAudioSystem();
    void resumeResetAudioSystem();
    void resetAllControllerRumble();
    bool setPermissionToCheckInput(bool);
    void onPauseBeginAllRumble();
    void onPauseEndAllRumble();
    void onHomeButtonMenuBeginAllRumble();
    void onHomeButtonMenuCloseAllRumble();
    void onHomeButtonMenuEndAllRumble();
    void prepareResetSystem();
    bool isPreparedFadeinSystem();
    void restartSceneController();
    bool isBeforeTitle();
};  // namespace GameSystemFunction
