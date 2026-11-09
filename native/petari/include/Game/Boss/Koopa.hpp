#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorJointCtrl;
class KoopaParts;
class KoopaSensorCtrl;
class KoopaSequencer;

class Koopa : public LiveActor {
public:
    Koopa(const char* pName, KoopaSequencer* pSequencer);

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    Vec3f mFront;
    KoopaSequencer* mSequencer;
    KoopaSensorCtrl* mSensorCtrl;
    KoopaParts* mParts;
    ActorJointCtrl* mJointCtrl;
};
