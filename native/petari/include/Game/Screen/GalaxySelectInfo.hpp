#pragma once

#include "Game/Screen/LayoutActor.hpp"

class GalaxyInfoLayoutSetter;
class ValueControl;

class GalaxySelectInfo : public LayoutActor {
public:
    GalaxySelectInfo(bool);

    virtual void init(const TableIter& rIter) override;
    virtual void draw() const override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;

    bool show(const char*, u32, int);
    bool show(const char*, u32, bool, int);
    void exeWait();
    void exeDisappear();

    bool mIsOnPause;  // drawn in the pause layer
    const char* mShownGalaxyName;
    bool mIsShownThisFrame;  // show() ran since the last control()
    ValueControl* mFadeinoutControl;
    GalaxyInfoLayoutSetter* mInfoLayoutSetter;
};
