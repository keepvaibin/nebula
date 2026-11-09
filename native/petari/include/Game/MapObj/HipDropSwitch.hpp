#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class CollisionParts;
class MapObjConnector;
class SpringValue;

class HipDropSwitch : public LiveActor {
public:
    HipDropSwitch(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void calcAnim() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

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
