#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class MapObjConnector;

class ValveSwitch : public LiveActor {
public:
    ValveSwitch(const char*);

    virtual ~ValveSwitch() {
    }
    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeAdjust();
    void exeValve();
    void exeEnd();
    void updateBindActorMtx();

    LiveActor* mBindActor;
    Pos3f mValveMtx;
    MapObjConnector* mObjConnector;
    bool mIsReverse;
};
