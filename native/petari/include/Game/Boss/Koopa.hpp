#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorJointCtrl;
class KoopaParts;
class KoopaSensorCtrl;
class KoopaSequencer;

class Koopa : public LiveActor {
public:
    Koopa(const char* pName, KoopaSequencer* pSequencer);

    virtual void init(const TableIter& rIter);
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    Vec3f mFront;
    KoopaSequencer* mSequencer;
    KoopaSensorCtrl* mSensorCtrl;
    KoopaParts* mParts;
    ActorJointCtrl* mJointCtrl;
};
