#pragma once

#include "Game/Boss/TripodBossFixParts.hpp"

class MapPartsRailMover;

class TripodBossRailMoveParts : public TripodBossFixParts {
public:
    TripodBossRailMoveParts(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void makeActorDead() override;
    virtual void calcTripodLocalMatrix(Pos3f*) override;
    virtual void activateTripodBoss() override;
    virtual void exeWaitOwn() override;

    MapPartsRailMover* mRailMover;
};
