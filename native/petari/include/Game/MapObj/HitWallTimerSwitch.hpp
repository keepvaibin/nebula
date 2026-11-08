#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MapObjConnector;

class HitWallTimerSwitch : public LiveActor {
public:
    HitWallTimerSwitch(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void calcAnim();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    bool trySwitchDown();
    bool tryOn();
    bool trySwitchUp();
    bool tryOff();
    void exeOff();
    void exeSwitchDown();
    void exeOn();
    void exeSwitchUp();

    CollisionParts* mMoveCollision;
    MapObjConnector* mConnector;
    s32 mTimer;
    u8 mIsHit;
};
