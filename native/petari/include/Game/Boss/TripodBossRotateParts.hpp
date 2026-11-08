#pragma once

#include "Game/Boss/TripodBossFixParts.hpp"

class MapPartsRotator;

class TripodBossRotateParts : public TripodBossFixParts {
public:
    TripodBossRotateParts(const char* pName);

    virtual void init(const TableIter&);
    virtual void makeActorDead();
    virtual void calcTripodLocalMatrix(Pos3f*);
    virtual void activateTripodBoss();
    virtual void exeWaitOwn();

    MapPartsRotator* mRotator;
};
