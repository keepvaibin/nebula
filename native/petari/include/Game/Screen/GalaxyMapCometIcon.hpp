#pragma once

#include "Game/Screen/LayoutActor.hpp"

namespace Ui {
    class DrawInfo;
}  // namespace Ui

class GalaxyMapCometIcon : public LayoutActor {
public:
    GalaxyMapCometIcon(const char*, LayoutActor*, const char*);

    virtual ~GalaxyMapCometIcon() override;
    virtual void init(const TableIter&) override;
    virtual void calcAnim() override;
    virtual void appear() override;
    virtual void control() override;

    void calcAnimForCapture(const Ui::DrawInfo&);
    void syncStatus();

private:
    const char* mGalaxyName;
    LayoutActor* mHost;
    const char* mPaneName;
};
