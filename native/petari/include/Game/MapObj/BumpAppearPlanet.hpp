#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class BumpAppearPlanet : public MapObjActor {
public:
    BumpAppearPlanet(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override;

    void exeWait();
    void exeBumpOut();
    void exeBumpIn();
    void startBumpOut();
    void startBumpIn();

    CollisionParts* mCollisionB;
    CollisionParts* mCollisionC;
};
