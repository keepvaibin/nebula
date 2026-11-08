#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace Ui {
    class DrawInfo;
}  // namespace Ui

class GalaxyNamePlate;

class GalaxyMapDomeIcon : public LayoutActor {
public:
    GalaxyMapDomeIcon(int, LayoutActor*, const char*, const char*);

    virtual void init(const TableIter& rIter);
    virtual void movement();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void appear();
    virtual void kill();
    virtual void control();

    void calcAnimForCapture(const Ui::DrawInfo&);
    void setModeNormal();
    void setModeNewDomeDiscover();
    s32 calcDomeStatus() const;
    bool isComplete() const;
    void syncStatus();
    bool hasNewGalaxy() const;

    int mDomeNo;
    LayoutActor* mHost;
    const char* mPaneName;
    const char* mNamePaneName;
    GalaxyNamePlate* mNamePlate;
    u8 mMode;
};
