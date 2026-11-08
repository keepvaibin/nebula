#pragma once

#include "Game/Scene/SceneFactory.hpp"
#include "Game/System/NerveExecutor.hpp"

class FindingLuigiEventScheduler;
class GalaxyCometScheduler;
class GalaxyMoveArgument;
class LuigiLeftSupplier;
class PlayerMissLeft;
class StarPointerOnOffController;
class StorySequenceExecutor;

class GameSequenceProgress : public NerveExecutor {
public:
    GameSequenceProgress();

    void initAfterResourceLoaded();
    void update();
    void draw();
    void startScene();
    void endScene();
    void requestChangeScene(SceneKind);
    void requestGalaxyMove(const GalaxyMoveArgument&);
    void requestCancelScenarioSelect();
    GalaxyCometScheduler* getGalaxyCometScheduler();

    void exeBooting();
    void exeBoot();
    void exeWaitGoToFirstScene();
    void exeNormal();
    void exeGalaxyMove();
    void exeResetProcessing();

    inline static bool isCometStar(GalaxyMoveArgument);
    static bool isScenePermittedUpdateInputStatus();
    void updateGameDataBeforeChangeScene();
    void resetGameDataAfterChangeScene(const GalaxyMoveArgument&);
    void updateGameDataGalaxyVisitedFlag();
    void setMinFrameBeforeStartNextStage(const GalaxyMoveArgument&);
    bool isLuigiLeftSupply();
    void offLuigiLeftSupply();
    void countDownGameEventValueFromNewPowerStar();

    StarPointerOnOffController* mStarPointerOnOffController;
    StorySequenceExecutor* mStorySequenceExecutor;
    FindingLuigiEventScheduler* mFindingLuigiEventScheduler;
    GalaxyCometScheduler* mGalaxyCometScheduler;
    LuigiLeftSupplier* mLuigiLeftSupplier;
    PlayerMissLeft* mPlayerMissLeft;
    s32 mMinFrame;
    bool mIsPlayTicoSound;
    bool mIsCancelScenarioSelect;
    bool mIsForceWipe;
};
