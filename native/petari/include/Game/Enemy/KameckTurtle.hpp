#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class JetTurtle;
class SmallKameckBeamEventListener;

class KameckTurtle : public LiveActor {
public:
    /// @brief Creates a new `KameckTurtle`.
    /// @param pName A pointer to the null-terminated name of the object.
    KameckTurtle(const char* pName);

    virtual void init(const TableIter&);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
