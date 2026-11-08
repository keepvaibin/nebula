#pragma once

#include "Game/Boss/TripodBossFixParts.hpp"

class MapPartsRailMover;

class TripodBossRailMoveParts : public TripodBossFixParts {
public:
    TripodBossRailMoveParts(const char* pName);

    virtual void init(const TableIter&);
    virtual void makeActorDead();
    virtual void calcTripodLocalMatrix(Pos3f*);
    virtual void activateTripodBoss();
    virtual void exeWaitOwn();

    MapPartsRailMover* mRailMover;
};
