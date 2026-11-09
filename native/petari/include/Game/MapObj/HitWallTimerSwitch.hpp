#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MapObjConnector;

class HitWallTimerSwitch : public LiveActor {
public:
    HitWallTimerSwitch(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void calcAnim() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

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
