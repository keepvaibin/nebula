#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorJointCtrl;
class KoopaParts;
class KoopaSensorCtrl;
class KoopaSequencer;

class Koopa : public LiveActor {
public:
    Koopa(const char* pName, KoopaSequencer* pSequencer);

    virtual ~Koopa();
    virtual void init(const JMapInfoIter& rIter);
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void updateHitSensor(HitSensor* pSensor);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    TVec3f mFront;
    KoopaSequencer* mSequencer;
    KoopaSensorCtrl* mSensorCtrl;
    KoopaParts* mParts;
    ActorJointCtrl* mJointCtrl;
};
