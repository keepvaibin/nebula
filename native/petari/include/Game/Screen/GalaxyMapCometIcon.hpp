#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace Ui {
    class DrawInfo;
}  // namespace Ui

class GalaxyMapCometIcon : public LayoutActor {
public:
    GalaxyMapCometIcon(const char*, LayoutActor*, const char*);

    virtual ~GalaxyMapCometIcon();
    virtual void init(const TableIter&);
    virtual void calcAnim();
    virtual void appear();
    virtual void control();

    void calcAnimForCapture(const Ui::DrawInfo&);
    void syncStatus();

private:
    const char* mGalaxyName;
    LayoutActor* mHost;
    const char* mPaneName;
};
