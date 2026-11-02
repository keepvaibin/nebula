#pragma once

#include "Game/Scene/MultiSceneActor.hpp"

class ScenarioSelectStar : public MultiSceneActor {
public:
    ScenarioSelectStar(EffectSystem*);

    virtual void init(const JMapInfoIter&);
    virtual void calcViewAndEntry();
    virtual void appear();
    virtual void kill();
    virtual void control();

    void tryPointing();
    void select();
    void notSelect();
    bool isAppearEnd() const;
    void setup(s32, int, const TVec3f&, s32);
    bool tryEndPointing();
    void updatePos();
    void exeAppear();
    void exeNotPointing();
    void exePointing();
    void exeEndPointing();
    void exeSelected();
    void exeSelectedMove();
    void exeNotSelected();

    int mStarCollectedStatus;
    s32 mScenarioNo;
    s32 mStarId;
    bool mIsPointing;
    f32 mRotateSpeed;
    s32 mRotateTime;
    TVec3f mBasePos;
    TVec3f mTranslationOnSelect;
    int mAppearFrame;
    f32 mScaleOnSelect;
};
