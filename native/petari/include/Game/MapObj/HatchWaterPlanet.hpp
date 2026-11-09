#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CollisionParts;
class LodCtrl;

class HatchWaterPlanet : public LiveActor {
public:
    HatchWaterPlanet(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    void exeWait();
    void exeOpen();
    void exeWaitAfterOpen();

    LodCtrl* mPlanetLODCtrl;
    CollisionParts* mCollisionParts;
};
