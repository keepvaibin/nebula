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

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void calcAnim();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    void initModelAndCollision(const TableIter& rIter);
    bool tryOn();
    bool trySwitchDown();
    bool tryConnect();
    void updateTimerSE();
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
    s32 mTimer;
    const char* mObjName;
};
