#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CountUpPaneRumbler;
class CounterLayoutAppearer;

class PlayerMissLeft : public LayoutActor {
public:
    PlayerMissLeft(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;

    void exeAppearWait();
    void exeAppear();
    void exeWait();
    void exeEnd();

private:
    CounterLayoutAppearer* mLayoutAppearer;
    CountUpPaneRumbler* mPaneRumbler;
};
