#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class SandCapsuleInsidePlanet : public MapObjActor {
public:
    SandCapsuleInsidePlanet(const char* pName);

    virtual void init(const TableIter&);
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&);

    void startOn();
    void startOff();

    void exeWait();
};
