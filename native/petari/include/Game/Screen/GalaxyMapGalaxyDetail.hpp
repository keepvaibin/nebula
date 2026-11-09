#pragma once

#include "Game/Screen/LayoutActor.hpp"

class GalaxyInfoLayoutSetter;
class IconAButton;

class GalaxyMapGalaxyDetail : public LayoutActor {
public:
    GalaxyMapGalaxyDetail();

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;
    virtual void draw() const override;
    virtual void calcAnim() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;

    void startDisplay(const char*, bool);
    void exeAppear();
    void exeDisplay();
    void exeDisappear();

    GalaxyInfoLayoutSetter* mInfoLayoutSetter;
    IconAButton* mAButtonIcon;
};
