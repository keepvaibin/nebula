#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CollisionParts;
class MapObjConnector;

class ScrewSwitch : public LiveActor {
public:
    ScrewSwitch(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeAdjust();
    void exeScrew();
    void exeEnd();
    void updateBindActorMtx();

    CollisionParts* mCollisionParts;
    LiveActor* mBindActor;
    MapObjConnector* mMapObjConnector;
};
