#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Koopa;

class KoopaFireShort : public LiveActor {
public:
    KoopaFireShort(const Koopa* pKoopa);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;

    void emitNormal();
    void emitFast();
    void emitCurve();
    void emitLongTime();

    void exeFly();
    void exeDisappear();
    void exeErase2P();

    void updateFly();
    void updateFrontAndVelocity();

private:
    const Koopa* mKoopa;
    Vec3f mFront;
    f32 mSpeed;
    s32 mDuration;
    f32 mOffset;
};
