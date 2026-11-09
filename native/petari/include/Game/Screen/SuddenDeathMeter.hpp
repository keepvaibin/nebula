#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CountUpPaneRumbler;

class SuddenDeathMeter : public LayoutActor {
public:
    SuddenDeathMeter(const char*, const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;

    void requestActive();
    void requestDeactivate();
    void requestPowerUp();
    void requestForceAppear();
    void requestPlayerMoving();
    void exeAppear();
    void exeWait();
    void exePowerUp();
    void exeMeterMove();
    void exeCounterMove();
    void exeBreakMeter();
    void exeZeroMeter();
    void exeZeroMeterBreak();
    void initCount(s32);
    void setCount(s32);
    void calcMarioHeadPosition(Vec2f*) const;
    void calcPowerUpMeterBasePosition(Vec2f*) const;
    void setPowerUpMeterPosition(const Vec2f&);
    void setAnimBase();
    void setCountAnimFrame();
    void setRecoveryCountAnimFrame();

    CountUpPaneRumbler* mCountUpPaneRumbler;
    Vec2f mFollowPosW;
    Vec2f mMeterMoveStartPos;
    s32 mCount;
    f32 mAlpha;
    f32 mShowHideRate;
};
