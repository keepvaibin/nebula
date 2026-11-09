#pragma once

#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/MapObj/MapObjConnector.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/SpringValue.hpp"

class DesertLandMoveSwitch : public LiveActor {
public:
    DesertLandMoveSwitch(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void calcAnim() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    void initModelAndCollision(const TableIter& rIter);
    bool tryOn();
    bool trySwitchDown();
    bool tryConnect();
    void updateTimerSE();
    bool updateSpring();
    void exeWait();
    void exeSwitchDown();
    void exeOn();
    void exeReturn();

    CollisionParts* mCollisionParts;
    SpringValue* mSpringValue;
    MapObjConnector* mMapObjConnector;
    bool mIsHipDropped;
    bool mWasOnPlayer;
    bool mIsOnPlayer;
    // Steps switch A stays on; 0 holds until something turns it off.
    s32 mTimer;
    const char* mObjName;
    // Added to the step for the countdown sounds.
    s32 mTimerLead;
};
