#pragma once

#include "Game/Scene/Scene.hpp"

class GameStageClearSequence;
class GamePauseSequence;
class GameSceneScenarioOpeningCameraState;
class GameScenePauseControl;

class GameScene : public Scene {
public:
    GameScene();

    virtual ~GameScene() override;
    virtual void init() override;
    virtual bool initAfterScenarioChosen() override;
    virtual void start() override;
    virtual void update() override;
    virtual void draw() const override;
    virtual void calcAnim() override;

    void notifyEndScenarioStarter();
    void requestPlayMovieDemo();
    void requestStartGameOverDemo();
    void requestEndGameOverDemo();
    void requestEndMissDemo();
    void requestPowerStarGetDemo();
    void requestGrandStarGetDemo();
    void setNerveAfterPauseMenu();
    bool isExecScenarioOpeningCamera() const;
    bool isExecScenarioStarter() const;
    bool isExecStageClearDemo() const;
    // The scene's own pause menu or galaxy map is up (LumaCore's time state MENU, services/time.md 5).
    bool isExecPauseMenuOrGalaxyMap() const;
    void exeScenarioOpeningCamera();
    void exeScenarioStarter();
    void exeSceneAction();
    void exePauseMenu();
    void exePowerStarGet();
    void exeGrandStarGet();
    void exeGameOver();
    void exeCometRetryAfterMiss();
    void exeSaveAfterGameOver();
    void exePlayMovie();
    void exeGalaxyMap();
    void exeStaffRoll();
    void exeNebulaBoard();
    void initSequences();
    void initEffect();
    void drawMirror() const;
    void draw3D() const;
    void draw2D() const;
    bool isValidScenarioOpeningCamera() const;
    void startStagePlayFirst();
    void startStagePlayRetry();
    bool isPermitToPauseMenu() const;
    void requestShowGalaxyMap();
    // Nebula's achievement board (the Observatory's Achievement Luma): the world held while it is open.
    void requestShowNebulaBoard();
    // Over everything the game draws, the system's wipes included (GameSystem::draw, under the settings menu): Nebula's
    // achievement pop-up.
    void drawOverlay() const;
    void requestStaffRoll();
    bool isDrawMirror() const;
    void stageClear();
    inline bool isPlayMovie() const;

    GameSceneScenarioOpeningCameraState* mScenarioCamera;
    GameScenePauseControl* mPauseCtrl;
    GamePauseSequence* mPauseSeq;
    GameStageClearSequence* mStageClearSeq;
    bool mDraw3D;
    unsigned mInitializePhase = 0;
    unsigned mReservePiecesLeft = 70;
};
