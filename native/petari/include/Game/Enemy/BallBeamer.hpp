#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class RingBeam;

class BallBeamer : public LiveActor {
public:
    BallBeamer(const char*);

    virtual void init(const TableIter&) override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void syncSwitchOnA();
    void syncSwitchOffA();
    void syncSwitchOnB();
    void setupAttack();
    bool tryAttack();
    void exeWait();
    void exeAttack();
    void exeInter();

    RingBeam** mBeams;
    bool mIsSwitchOn;
    // needed to get mHeadCollisionMtx at correct offset
    Mtx34f mHeadCollisionMtx;
};
