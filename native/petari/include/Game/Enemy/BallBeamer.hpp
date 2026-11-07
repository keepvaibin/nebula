#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class RingBeam;

class BallBeamer : public LiveActor {
public:
    BallBeamer(const char*);

    virtual void init(const TableIter&);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void syncSwitchOnA();
    void syncSwitchOffA();
    void syncSwitchOnB();
    void setupAttack();
    bool tryAttack();
    void exeWait();
    void exeAttack();
    void exeInter();

    RingBeam** mBeams;
    bool _90;
    // needed to get _98 at correct offset
    u8 _94[4];  // unused?
    Mtx34f _98;
};
