#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CountUpPaneRumbler;
class CounterLayoutAppearer;

class PurpleCoinCounter : public LayoutActor {
public:
    PurpleCoinCounter(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void control();

    void validate();
    void updateCounter();
    void exeAppear();
    void exeWait();

private:
    s32 mPurpleCoinNum;
    s32 mPurpleCoinDisplayNum;
    s32 mInvalidCountUpFrame;
    CounterLayoutAppearer* mLayoutAppearer;
    CountUpPaneRumbler* mPaneRumbler;
    bool mIsValid;
};
