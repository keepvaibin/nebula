#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ModelObj;
class RingBeam;

class RingBeamer : public LiveActor {
public:
    RingBeamer(const char*);

    virtual void init(const TableIter&) override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void syncSwitchOffB();
    void setupAttack();
    bool tryAttack();
    void exeWait();
    void exeAttack();
    void exeInter();

    RingBeam** mBeams;
};
