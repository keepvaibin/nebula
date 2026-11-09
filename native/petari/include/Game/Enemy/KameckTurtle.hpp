#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class JetTurtle;
class SmallKameckBeamEventListener;

class KameckTurtle : public LiveActor {
public:
    KameckTurtle(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initJetTurtle();
    void appearDirection(const Vec3f&);
    void exeRun();
    void exeWait();
    bool isEnableAttack() const;
    void appearJetTurtle(bool);

    SmallKameckBeamEventListener* mEventListener;
    JetTurtle* mTurtle;
    Quat4f mRotateQuat;
    Vec3f mFront;
    Vec3f mRunDir;
};
