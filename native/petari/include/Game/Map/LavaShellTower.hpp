#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class LavaShellTower : public MapObjActor {
public:
    LavaShellTower(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override {};

    void exeWait();
    void exeDemo();
    void exeDone();
};
