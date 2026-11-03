#pragma once

#include "Game/System/NerveExecutor.hpp"

class GameSequenceDirector;
class GameSystemErrorWatcher;
class GameSystemFontHolder;
class GameSystemFrameControl;
class GameSystemObjHolder;
class GameSystemSceneController;
class GameSystemStationedArchiveLoader;
class HomeButtonLayout;
class HomeButtonStateNotifier;
class SystemWipeHolder;


class GameSystem : public NerveExecutor {
public:
    /// @brief Creates a new `GameSystem`.
    GameSystem();

    void init();
    bool isExecuteLoadSystemArchive() const;
    bool isDoneLoadSystemArchive() const;
    void startToLoadSystemArchive();
    void exeInitializeAudio();
    void exeInitializeLogoScene();
    void exeLoadStationedArchive();
    void exeWaitForReboot();
    void exeNormal();
    void initGX();
    void initAfterStationedResourceLoaded();
    void prepareReset();
    bool isPreparedReset() const;
    void frameLoop();
    void drawFrame();
    void draw();
    void update();
    void updateSceneController();
    void calcAnim();

    GameSequenceDirector* mSequenceDirector;
    GameSystemErrorWatcher* mErrorWatcher;
    GameSystemFontHolder* mFontHolder;
    GameSystemFrameControl* mFrameControl;
    GameSystemObjHolder* mObjHolder;
    GameSystemSceneController* mSceneController;
    GameSystemStationedArchiveLoader* mStationedArchiveLoader;
    HomeButtonLayout* mHomeButtonLayout;
    SystemWipeHolder* mSystemWipeHolder;
    HomeButtonStateNotifier* mHomeButtonStateNotifier;
    bool mIsExecuteLoadSystemArchive;
};
