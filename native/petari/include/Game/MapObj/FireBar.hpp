#pragma once

#include "Game/LiveActor/ModelObj.hpp"

class FireBarBall : public ModelObj {
public:
    FireBarBall(LiveActor*);

    virtual ~FireBarBall();
    virtual void init(const JMapInfoIter&);
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

    virtual ~FireBar();
    virtual void init(const JMapInfoIter&);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void updateHitSensor(HitSensor*);
    virtual void attackSensor(HitSensor*, HitSensor*);

    void exeWait();
    void initFireBarBall(const JMapInfoIter&);
    void fixFireBarBall();

    FireBarBall** mFireBalls;
    s32 mFireBallCount;
    TVec3f _94;
    f32 mFireBarSpeed;
    s32 mStickCount;
    f32 mStickDistance;
};
