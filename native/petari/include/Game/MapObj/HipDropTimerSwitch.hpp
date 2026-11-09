#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class SpringValue;
class MapObjConnector;
class CollisionParts;

class HipDropTimerSwitch : public LiveActor {
public:
    HipDropTimerSwitch(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void calcAnim() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void updateTimerSE();
    void exeOff();
    void exeSwitchDown();
    void exeOn();
    void exeSwitchUp();

    SpringValue* mSpring;
    MapObjConnector* mMapObjConnector;
    CollisionParts* mCollisionParts;
    s32 mTimerMax;      // In frames
    s32 mTimerSFXType;  // 0: A ticking, 1: B ticking
    bool mWasLightPressed;
    bool mIsLightPressed;
};
