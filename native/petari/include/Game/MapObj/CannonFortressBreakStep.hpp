#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class MapObjActorInitInfo;

class CannonFortressBreakStep : public MapObjActor {
public:
    CannonFortressBreakStep(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override;
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) override;

    void exeWait();
    void exeFall();
    void exeFallStart();
    void exeBreak();
    void startFall();
};
