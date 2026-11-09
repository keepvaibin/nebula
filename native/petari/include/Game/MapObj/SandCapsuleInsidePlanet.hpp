#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class SandCapsuleInsidePlanet : public MapObjActor {
public:
    SandCapsuleInsidePlanet(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override;

    void startOn();
    void startOff();

    void exeWait();
};
