#pragma once

#include "Game/Scene/SceneFactory.hpp"

class IntermissionScene;
class PlacementId;
class NameObjHolder;
class NameObjListExecutor;
class Nerve;
class ScenarioDataParser;
class ScenarioSelectScene;
class Scene;
class SceneObjHolder;
class Spine;

class SceneControlInfo {
public:
    SceneControlInfo();

    void setStage(const char*);
    void setStartIdInfo(const PlacementId&);

    SceneKind mScene;
    const char* mStage;
    s32 mScenarioNo;
    s32 mSelectedScenarioNo;
    PlacementId* mStartIdInfo;
};

enum SceneInitializeState {
    SceneInitializeState_NotInit,
    SceneInitializeState_Init,
    SceneInitializeState_PlacementPlayer,
    SceneInitializeState_PlacementHighPriority,
    SceneInitializeState_Placement,
    SceneInitializeState_AfterPlacement,
    SceneInitializeState_End,
};

class GameSystemSceneController {
public:
    void holdSceneInitialize();
    bool resumeSceneInitialize();
    GameSystemSceneController();

    void initAfterSystemArchiveLoaded();
    void requestChangeScene();
    void checkRequestAndChangeScene();
    void initializeScene();
    void destroyScene();
    bool isStopSound() const;
    bool isReadyToStartScene() const;
    bool isFirstUpdateSceneNerveNormal() const;
    // A scene runs (not being built, taken down or waited for): LumaCore's time state (services/time.md 5).
    bool isNerveNormal() const;
    void startScene();
    void updateScene();
    void updateSceneDuringResetProcessing();
    void calcAnimScene();
    void drawScene();
    // The game scene's overlay (GameScene::drawOverlay), after the system's wipes.
    void drawSceneOverlay();
    NameObjListExecutor* getNameObjListExecutor() const;
    SceneObjHolder* getSceneObjHolder() const;
    bool isExistSceneObjHolder() const;
    s32 getCurrentScenarioNo() const;
    s32 getCurrentSelectedScenarioNo() const;
    void setSceneInitializeState(SceneInitializeState);
    bool isSceneInitializeState(SceneInitializeState) const;
    void startScenarioSelectScene();
    void startScenarioSelectSceneBackground();
    void setCurrentScenarioNo(s32, s32);
    void resetCurrentScenarioNo();
    bool isScenarioDecided() const;
    void exeNotInitialized();
    void exeNormal();
    void exeWaitDrawDoneScene();
    void exeDestroyScene();
    void exeChoosePlayerVoices();
    void exeInitializeScene();
    void exeInvalidateSystemWipe();
    void exeDestroyed();
    void exeReadyToStartScene();
    void prepareReset();
    bool isPreparedReset();
    void restartGameAfterResetting();
    bool isExistRequest() const;
    bool isSameAtNextSceneAndStage() const;
    void updateSceneControlInfo();
    Scene* getCurrentSceneForExecute() const;
    const Nerve* getNextNerveOnResetProcessing() const;
    // Drops the scene files' resource holders and returns true, unless (not forced) the next scene is the same scene and
    // stage: then they stay for it.
    bool releaseSceneFiles(bool isForce);
    void requestChangeNerve(const Nerve*);

    bool isCurrentNerve(const Nerve*) const;
    bool isNextNerve(const Nerve*) const;

    SceneControlInfo mCurrSceneControlInfo;
    SceneControlInfo mNextSceneControlInfo;
    Spine* mSpine;
    const Nerve* mNextNerve;
    bool mIsResetProcessing;
    ScenarioDataParser* mScenarioParser;
    NameObjHolder* mObjHolder;
    Scene* mScene;
    SceneInitializeState mSceneInitializeState;
    bool mSceneInitializeHeld = false;
    bool mSceneInitializeDone = false;
    IntermissionScene* mIntermissionScene;
    ScenarioSelectScene* mScenarioSelectScene;
};
