#pragma once

#include "Game/Scene/MultiSceneActor.hpp"

class ScenarioSelectStar : public MultiSceneActor {
public:
    ScenarioSelectStar(EffectSystem*);

    virtual void init(const TableIter&) override;
    virtual void calcViewAndEntry() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;

    void tryPointing();
    void select();
    void notSelect();
    bool isAppearEnd() const;
    void setup(s32, int, const Vec3f&, s32);
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
    Vec3f mBasePos;
    Vec3f mTranslationOnSelect;
    int mAppearFrame;
    f32 mScaleOnSelect;
};
