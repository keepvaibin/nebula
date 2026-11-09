#pragma once

#include "Game/System/NerveExecutor.hpp"

class GameSequenceDirector;
class GameSystemErrorWatcher;
class GameSystemFontHolder;
class GameSystemObjHolder;
class GameSystemSceneController;
class HomeButtonLayout;
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
    bool mIsExecuteLoadSystemArchive;
    bool mWasSceneHeld;  // last frame, the HOME Menu, a warning, the skip's question or the settings menu held the scene
    bool mIsSceneResumed;  // this frame the scene goes on after such a hold
};
