#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class MapObjConnector;

class ValveSwitch : public LiveActor {
public:
    ValveSwitch(const char*);

    virtual ~ValveSwitch() override {
    }
    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

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
