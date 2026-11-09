#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class CollisionParts;

class SandUpDownTriRock : public MapObjActor {
public:
    SandUpDownTriRock(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override;

    void startDown();

    void exeWait();
    void exeDown();
    void exeRest();
    void exeUp();
    void exeStop();

    CollisionParts* mRockCollisionParts[3];
    s32 mRestStep;
};
