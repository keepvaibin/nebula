#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CollisionParts;
class MapObjConnector;

class ScrewSwitch : public LiveActor {
public:
    ScrewSwitch(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeAdjust();
    void exeScrew();
    void exeEnd();
    void updateBindActorMtx();

    CollisionParts* mCollisionParts;
    LiveActor* mBindActor;
    MapObjConnector* mMapObjConnector;
};
