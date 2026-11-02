#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class JetTurtle;
class SmallKameckBeamEventListener;

class KameckTurtle : public LiveActor {
public:
    /// @brief Creates a new `KameckTurtle`.
    /// @param pName A pointer to the null-terminated name of the object.
    KameckTurtle(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initJetTurtle();
    void appearDirection(const TVec3f&);
    void exeRun();
    void exeWait();
    bool isEnableAttack() const;
    void appearJetTurtle(bool);

    SmallKameckBeamEventListener* mEventListener;
    JetTurtle* mTurtle;
    TQuat4f mRotateQuat;
    TVec3f _A4;
    TVec3f _B0;
};
