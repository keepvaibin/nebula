#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/MapObj/MapObjConnector.hpp"

class PTimerSwitch : public LiveActor {
public:
    PTimerSwitch(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void calcAnim() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void reset();
    bool trySwitchDown();
    bool tryOn();
    void exeOff();
    void exeSwitchDown();
    void exeOn();

    CollisionParts* mSwitchCollision;
    MapObjConnector* mConnector;
    s32 mPressStep;
    bool mIsPressed;
};
