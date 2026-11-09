#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class ReverseGravityRoomPlanet : public MapObjActor {
public:
    ReverseGravityRoomPlanet(const char*);

    virtual void init(const TableIter&) override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override;

    void exeDownStart();
    void exeDownWait();
    void exeUpStart();
    void exeUpWait();
    void startSwitchOn();
    void startSwitchOff();
};
