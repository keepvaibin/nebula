#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class CollisionParts;
class MapObjConnector;
class SpringValue;

class HipDropSwitch : public LiveActor {
public:
    HipDropSwitch(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void calcAnim();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initStarPointerTarget();
    bool trySwitchDown();
    bool tryOn();

    void exeOff();
    void exeSwitchDown();
    void exeOn();

    SpringValue* mSpringValue;
    MapObjConnector* mConnector;
    CollisionParts* mSpringModel;
    bool mIsHipDropped;
    bool mWasOnPlayer;
    bool mIsOnPlayer;
};
