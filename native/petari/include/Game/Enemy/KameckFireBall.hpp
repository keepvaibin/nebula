#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KameckBeamEventListener;

class KameckFireBall : public LiveActor {
public:
    KameckFireBall(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void appearDirection(const Vec3f&);
    void setEventListener(KameckBeamEventListener*);

    void exeWait();
    void exeSweep();

    Quat4f mRotateQuat;
    KameckBeamEventListener* mEventListener;
};
