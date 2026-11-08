#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelObj.hpp"

class PrizeRing : public ModelObj {
public:
    PrizeRing();

    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void kill();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void setLife(s32 lifeTime);
    void setNumber(s32 num);
    void setNervePass();
    bool isReadyToPass() const;
    bool isReadyToKill() const;
    void exeStart();
    void exeLoop();
    void exeTimeout();
    void exeReadyToKill();
    void exeReadyToPass();
    void exePass();
    bool isPassed() const;

private:
    s32 getNumber() const;
    void playSound() const;
    bool isOnTriggerTimeoutFlash() const;

    s32 mLifeTime;
};
