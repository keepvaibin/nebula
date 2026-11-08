#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class CollisionParts;

class SandUpDownTriRock : public MapObjActor {
public:
    SandUpDownTriRock(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void control();
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&);

    void startDown();

    void exeWait();
    void exeDown();
    void exeRest();
    void exeUp();
    void exeStop();

    CollisionParts* mRockCollisionParts[3];
    s32 mRestStep;
};
