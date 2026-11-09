#pragma once

#include "Game/Boss/TripodBossFixParts.hpp"

class MapPartsRotator;

class TripodBossRotateParts : public TripodBossFixParts {
public:
    TripodBossRotateParts(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void makeActorDead() override;
    virtual void calcTripodLocalMatrix(Pos3f*) override;
    virtual void activateTripodBoss() override;
    virtual void exeWaitOwn() override;

    MapPartsRotator* mRotator;
};
