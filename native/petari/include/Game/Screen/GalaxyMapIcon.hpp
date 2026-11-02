#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace nw4r {
    namespace lyt {
        class DrawInfo;
    };  // namespace lyt
};  // namespace nw4r

class ButtonPaneController;

class GalaxyMapIcon : public LayoutActor {
public:
    GalaxyMapIcon(const char*, LayoutActor*, const char*);

    virtual ~GalaxyMapIcon();
    virtual void init(const JMapInfoIter& rIter);
    virtual void calcAnim();
    virtual void appear();
    virtual void control();

    void calcAnimForCapture(const nw4r::lyt::DrawInfo&);
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
