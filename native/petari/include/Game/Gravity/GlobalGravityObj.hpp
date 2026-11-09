#pragma once

#include "Game/Gravity/GravityCreator.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class GlobalGravityObj : public LiveActor {
public:
    GlobalGravityObj(const char* pName);

    virtual ~GlobalGravityObj() override {
    }
    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;

    void updateSwitch();
    PlanetGravity* getGravity();

    GravityCreator* mGravityCreator;
};

namespace MR {
    NameObj* createGlobalCubeGravityObj(const char* pName);
    NameObj* createGlobalConeGravityObj(const char* pName);
    NameObj* createGlobalDiskGravityObj(const char* pName);
    NameObj* createGlobalDiskTorusGravityObj(const char* pName);
    NameObj* createGlobalPlaneGravityObj(const char* pName);
    NameObj* createGlobalPlaneInBoxGravityObj(const char* pName);
    NameObj* createGlobalPlaneInCylinderGravityObj(const char* pName);
    NameObj* createGlobalPointGravityObj(const char* pName);
    NameObj* createGlobalSegmentGravityObj(const char* pName);
    NameObj* createGlobalWireGravityObj(const char* pName);
};  // namespace MR
