#pragma once

#include "Game/System/NerveExecutor.hpp"

class GameSequenceDirector;
class GameSystemErrorWatcher;
class GameSystemFontHolder;
class GameSystemObjHolder;
class GameSystemSceneController;
class HomeButtonLayout;
class HomeButtonStateNotifier;
class SystemWipeHolder;


class GameSystem : public NerveExecutor {
public:
    GameSystem();

    void init();
    bool isExecuteLoadSystemArchive() const;
    bool isDoneLoadSystemArchive() const;
    void startToLoadSystemArchive();
    void exeInitializeAudio();
    void exeInitializeBootScene();
    void exeLoadSystemArchive();
    void exeNormal();
    void initAfterSystemArchiveLoaded();
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
    GameSystemObjHolder* mObjHolder;
    GameSystemSceneController* mSceneController;
    HomeButtonLayout* mHomeButtonLayout;
    SystemWipeHolder* mSystemWipeHolder;
    HomeButtonStateNotifier* mHomeButtonStateNotifier;
    bool mIsExecuteLoadSystemArchive;
};
