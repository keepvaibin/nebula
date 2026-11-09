#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class SpinPullParticleCallBack;

class Candlestand : public MapObjActor {
public:
    Candlestand(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void makeActorAppeared() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void emitEffectFire();
    void deleteEffectFire();
    void emitEffectExtinguishFire();
    void appearItem();
    void exeWaitFire();
    void exeFire();
    void exeExtinguish();
    void exeFlicker();
    inline void exeAttack();
    inline void exeBurn();

private:
    bool isBurning() const;
    bool tryFire(HitSensor* pSender, HitSensor* pReceiver);

public:
    s32 mItem;
    bool mHasItemAppear;
    SpinPullParticleCallBack* mSpinPtclCb;
};
