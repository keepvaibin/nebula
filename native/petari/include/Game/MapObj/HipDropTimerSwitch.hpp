#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class SpringValue;
class MapObjConnector;
class CollisionParts;

class HipDropTimerSwitch : public LiveActor {
public:
    HipDropTimerSwitch(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void calcAnim();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
