#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class SnowCapsulePlanet : public MapObjActor {
public:
    SnowCapsulePlanet(const char*);

    virtual void init(const TableIter&);

    void exeCloseWait();
    void exeOpenWait();
    void exeOpen();

    CollisionParts* mDoor1;
    CollisionParts* mDoor2;
};
