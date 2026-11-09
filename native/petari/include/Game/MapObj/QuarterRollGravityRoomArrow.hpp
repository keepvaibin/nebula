#pragma once

#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActor.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/Table.hpp"

class QuarterRollGravityRoomArrow : public MapObjActor {
public:
    QuarterRollGravityRoomArrow(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo& rInfo) override;
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo& rInfo) override;
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo& rInfo) override;

    void exeWait();
    void exeRollOnA();
    void exeRollOffA();
    void exeRollOnB();
    void exeRollOffB();
    void onRollA();
    void offRollA();
    void onRollB();
    void offRollB();
};
