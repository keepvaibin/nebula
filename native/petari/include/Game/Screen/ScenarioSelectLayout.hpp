#pragma once

#include "Game/Screen/LayoutActor.hpp"
#include <nebula_geometry.hpp>

class BackButton;
class CameraContext;
class MultiSceneActor;
class ScenarioSelectStar;

class ScenarioSelectLayout : public LayoutActor {
public:
    ScenarioSelectLayout(EffectSystem*, const CameraContext*);

    virtual void init(const TableIter&);
    virtual void movement();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void appear();
    virtual void kill();
    virtual void control();

    void calcViewAndEntryStarModel();
    s32 getSelectedScenarioNo() const;
    bool isReadyToDisappear() const;
    void disappear();
    bool isAppearStarEnd() const;
    bool isEndAnimStartStep() const;
    bool isCanceled() const;
    bool isDecided() const {
        return mIsDecided;
    }
    void updateSelectedScenario();
    void updateScenarioText();
    void fadeInText();
    void fadeOutText();
    bool trySelect();
    ScenarioSelectStar* getSelectedStar() const;
    void calcWorldPositionFromScreenPos(Vec3f*, const Vec2f&, f32) const;
    bool calcDisplayScenarioNum(s32*, s32*) const;
    void appearAllStar(s32, s32, bool);
    void appearStar(s32, s32, int);
    void appearPage(s32);
    void tryChangePage();
    bool isAppearStarEndAll() const;
    bool tryCancel();
    void setPlayerLeft();
    void startAnimAllNewPane(const char*);
    void setAnimRateAllNewPane(f32);

    void exeAppearStar();
    void exeAppear();
    void exeWaitScenarioSelect();
    void exeDecide();
    void exeAfterScenarioSelected();
    void exeDisappear();
    void exeCancel();
    void exeCancelFadeOut();
    void exeAppearCometWarning();
    void exeWaitCometWarning();
    void exeDisappearCometWarning();

    s32 mSelectedScenarioNo;
    bool mIsDecided; // a scenario was chosen: the stars stop following the pointer
    Mtx34f mEffectHostMtx;
    ScenarioSelectStar** mStar;
    MultiSceneActor* mScenarioSky;
    EffectSystem* mEffectSystem;
    const CameraContext* mCameraContext;
    Vec2f mNewTextFollowPos;
    Vec2f mNewGreenTextFollowPos;
    Vec2f mQuestionPane[2];
    Vec2f mStarTopFollowPos;
    BackButton* mBackButton;
    Vec2f mMarioPaneFollowPos;
    const char* mMarioPaneName;
    s32 mTextFadeState;
    s32 mTextScenarioNo;
    // A galaxy with more stars than the screen's seven: all of them, in order, and the page shown.
    s32* mListScenario = nullptr;
    s32* mListState = nullptr;
    s32 mListNum = 0;
    s32 mListPage = 0;
    bool mPageKeyHeld = false;
};
