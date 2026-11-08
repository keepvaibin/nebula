#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class MapObjActorInitInfo;

class CannonFortressBreakStep : public MapObjActor {
public:
    CannonFortressBreakStep(const char* pName);

    virtual void init(const TableIter&);
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&);
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&);

    void exeWait();
    void exeFall();
    void exeFallStart();
    void exeBreak();
    void startFall();
};
