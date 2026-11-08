#pragma once

#include "Game/LiveActor/ModelObj.hpp"

class FireBarBall : public ModelObj {
public:
    FireBarBall(LiveActor*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();

    void controlEmitEffect();

    LiveActor* mFireBarParent;
};

class FireBar : public LiveActor {
public:
    FireBar(const char*);

    virtual void init(const TableIter&);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void updateHitSensor(HitSensor*);
    virtual void attackSensor(HitSensor*, HitSensor*);

    void exeWait();
    void initFireBarBall(const TableIter&);
    void fixFireBarBall();

    FireBarBall** mFireBalls;
    s32 mFireBallCount;
    Vec3f mFront;
    f32 mFireBarSpeed;
    s32 mStickCount;
    f32 mStickDistance;
};
