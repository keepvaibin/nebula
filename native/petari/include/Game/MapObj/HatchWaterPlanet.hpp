#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CollisionParts;
class LodCtrl;

class HatchWaterPlanet : public LiveActor {
public:
    HatchWaterPlanet(const char*);

    virtual void init(const TableIter&);
    virtual void control();

    void exeWait();
    void exeOpen();
    void exeWaitAfterOpen();

    LodCtrl* mPlanetLODCtrl;
    CollisionParts* mCollisionParts;
};
