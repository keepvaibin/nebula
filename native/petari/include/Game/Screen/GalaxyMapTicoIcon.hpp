#pragma once

#include "Game/Screen/LayoutActor.hpp"

class GalaxyMapTicoIcon : public LayoutActor {
public:
    GalaxyMapTicoIcon(int, const char*, LayoutActor*, const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void appear();
    virtual void control();

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
