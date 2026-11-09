#pragma once

#include "Game/Screen/LayoutActor.hpp"

class GalaxyMapTicoIcon : public LayoutActor {
public:
    GalaxyMapTicoIcon(int, const char*, LayoutActor*, const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void control() override;

    void syncStatus();
    void setModeNormal();
    void setModeDiscover();
    bool isModeDiscoverAndIsAppear() const;

private:
    int mExclamationGalaxyIndex;
    const char* mGalaxyName;
    LayoutActor* mHost;
    char* mPaneName;
    u8 mMode;
};
