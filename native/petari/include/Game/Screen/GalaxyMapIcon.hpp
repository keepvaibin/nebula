#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace Ui {
    class DrawInfo;
}  // namespace Ui

class ButtonPaneController;

class GalaxyMapIcon : public LayoutActor {
public:
    GalaxyMapIcon(const char*, LayoutActor*, const char*);

    virtual ~GalaxyMapIcon() override;
    virtual void init(const TableIter& rIter) override;
    virtual void calcAnim() override;
    virtual void appear() override;
    virtual void control() override;

    void calcAnimForCapture(const Ui::DrawInfo&);
    void setModeNormal();
    void setModeNewGalaxyDiscover();
    bool isStatusNew() const;
    void onPointing();
    void activatePointing();
    void deactivatePointing();
    s32 examineIconStatus() const;
    bool isBlink(int) const;
    bool isNewOnDiscoverMode() const;
    bool isPointing() const;
    void syncStatus();

    ButtonPaneController* mButtonPaneController;
    const char* mGalaxyName;
    LayoutActor* mHost;
    const char* mPaneName;
    u8 mMode;
};
