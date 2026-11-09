#pragma once

#include "Game/Scene/Scene.hpp"

class CameraContext;
class CinemaFrame;
class EffectSystem;
class ItemBuffer;
class ScenarioSelectLayout;

class ScenarioSelectScene : public Scene {
public:
    ScenarioSelectScene();

    virtual void init();
    virtual void start();
    virtual void update();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void calcViewAndEntry();

    void startBackground();
    bool isActive() const;
    bool isExecForeground() const;
    bool isRunning() const {
        return isExecForeground() && mIsResetRequested == 0;
    }
    bool isScenarioSelecting() const;
    void validateScenarioSelect();
    void requestReset(bool waitForInitialize);
    bool isResetEnd() const;
    void setupCameraMtx() const;
    bool trySetCurrentScenarioNo() const;
    void suspend();
    void tryStartScreenToFrame();
    void exeDeactive();
    void exeInvalidScenarioSelect();
    void exeWaitStartScenarioSelect();
    void exeStartScenarioSelect();
    void exeWaitScenarioSelect();
    void exeWaitSceneLoad();
    void exeWaitInitializeEnd();
    void exeWaitDisappearLayout();
    void exeWaitSceneLoadIfRequestedReset();
    void exeWaitSceneLoadIfCanceledSelect();

    bool mIsInitialized;
    bool mIsBackground;
    bool mIsCanceled;
    ScenarioSelectLayout* mScenarioLayout;
    CinemaFrame* mCinemaFrame;
    ItemBuffer* mDrawBufferOpa;
    ItemBuffer* mDrawBufferXlu;
    bool mIsResetRequested;
    EffectSystem* mEffectSystem;
    CameraContext* mCameraContext;
};
