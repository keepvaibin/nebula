#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CountUpPaneRumbler;
class CounterLayoutAppearer;

class StarCounter : public LayoutActor {
public:
    /// @brief Creates a new `StarCounter`.
    StarCounter();

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void control();

    void disappear();
    bool isWait() const;
    void exeAppear();
    void exeWait();
    void exeCountUp();
    void exeDisappear();

private:
    s32 mPowerStarNum;
    CounterLayoutAppearer* mLayoutAppearer;
    CountUpPaneRumbler* mPaneRumbler;
};
