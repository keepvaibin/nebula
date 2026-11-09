#pragma once

#include "Game/LiveActor/ModelObj.hpp"

class FireBarBall : public ModelObj {
public:
    FireBarBall(LiveActor*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;

    void controlEmitEffect();

    LiveActor* mFireBarParent;
    bool mIsEffectNear = false;  // the player was within the effect's range last step (leaving takes a margin more)
};

class FireBar : public LiveActor {
public:
    FireBar(const char*);

    virtual void init(const TableIter&) override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void updateHitSensor(HitSensor*) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

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
