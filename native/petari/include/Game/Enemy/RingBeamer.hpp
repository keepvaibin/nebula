#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ModelObj;
class RingBeam;

class RingBeamer : public LiveActor {
public:
    RingBeamer(const char*);

    virtual void init(const TableIter&);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void syncSwitchOffB();
    void setupAttack();
    bool tryAttack();
    void exeWait();
    void exeAttack();
    void exeInter();

    RingBeam** mBeams;
};
