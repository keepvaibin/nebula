#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class LavaShellTower : public MapObjActor {
public:
    LavaShellTower(const char* pName);

    virtual void init(const TableIter&);
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) {};

    void exeWait();
    void exeDemo();
    void exeDone();
};
