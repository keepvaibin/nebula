#pragma once


class IntermissionScene;
class JMapIdInfo;
class NameObjHolder;
class NameObjListExecutor;
class Nerve;
class PlayTimerScene;
class ScenarioDataParser;
class ScenarioSelectScene;
class Scene;
class SceneObjHolder;
class Spine;

class SceneControlInfo {
public:
    /// @brief Creates a new `SceneControlInfo`.
    SceneControlInfo();

    void setScene(const char*);
    void setStage(const char*);
    void setStartIdInfo(const JMapIdInfo&);

    char mScene[32];
    char mStage[32];
    s32 mScenarioNo;
    s32 mSelectedScenarioNo;
    JMapIdInfo* mStartIdInfo;
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
    /// @brief Creates a new `GameSystemSceneController`.
    GameSystemSceneController();

    void initAfterStationedResourceLoaded();
    void requestChangeScene();
    void checkRequestAndChangeScene();
    void initializeScene();
    void destroyScene();
    bool isStopSound() const;
    bool isReadyToStartScene() const;
    bool isFirstUpdateSceneNerveNormal() const;
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
    bool tryDestroyFileCacheHeap(bool);
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
    PlayTimerScene* mPlayTimerScene;
    ScenarioSelectScene* mScenarioSelectScene;
};
