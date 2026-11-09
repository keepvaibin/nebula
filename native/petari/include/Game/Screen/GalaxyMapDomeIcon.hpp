#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace Ui {
    class DrawInfo;
}  // namespace Ui

class GalaxyNamePlate;

class GalaxyMapDomeIcon : public LayoutActor {
public:
    GalaxyMapDomeIcon(int, LayoutActor*, const char*, const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;
    virtual void draw() const override;
    virtual void calcAnim() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;

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
