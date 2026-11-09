#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CountUpPaneRumbler;
class CounterLayoutAppearer;

class PurpleCoinCounter : public LayoutActor {
public:
    PurpleCoinCounter(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;

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
