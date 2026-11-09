#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpinPullParticleCallBack;

class PhantomTorch : public LiveActor {
public:
    PhantomTorch(const char*);

    virtual void init(const TableIter&) override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void makeActorDead() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void soundOrder();
    void exeWait();
    void exeAttack();
    void exeExtinguish();

    const char* mParticle;
    s32 mItem;
    SpinPullParticleCallBack* mPullParticle;
    bool mIsDecorative;
    // The burning loop for this torch kind (bonfire or torch).
    const char* mBurnSe;
};
