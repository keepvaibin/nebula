#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CountUpPaneRumbler;

class MeterLayout : public LayoutActor {
public:
    /// @brief Creates a new `MeterLayout`.
    /// @param pName A pointer to the null-terminated name of the object.
    /// @param pArcName A pointer to the null-terminated name of the resource archive.
    MeterLayout(const char* pName, const char* pArcName);

    virtual void init(const TableIter& rIter);
    virtual void control();

    void requestActive();
    void requestDeactivate();
    void requestPowerUp();
    void requestForceAppear();
    void requestPlayerMoving();
    void requestPlayerStopped();
    void initCount(s32);
    void setCount(s32);
    void exeHide();
    void exeAppear();
    void exeWaitStart();
    void exeWait();
    void exePowerUp();
    void exeMeterMove();
    void exeCounterMove();
    void exeRecover();
    void exeDamage();
    void exeBreakMeter();
    void exeZeroMeter();
    void exeZeroMeterBreak();
    void calcMarioHeadPosition(Vec2f*) const;
    void calcPowerUpMeterBasePosition(Vec2f*) const;
    void setAnimBase();
    void setCountAnimFrame();
    void setRecoveryCountAnimFrame();

    CountUpPaneRumbler* mCountUpPaneRumbler;
    Vec2f mFollowPos;
    Vec2f mFollowPosW;
    Vec2f mMeterMoveStartPos;
    s32 mCount;
    f32 mAlpha;
    f32 mVisibleRate;
};
