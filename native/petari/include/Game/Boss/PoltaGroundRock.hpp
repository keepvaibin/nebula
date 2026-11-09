#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Polta;
class ModelObj;

class PoltaGroundRock : public LiveActor {
public:
    PoltaGroundRock(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void initBreakModel();
    void start(LiveActor*, const Vec3f&);
    void requestBreak();
    bool isEnableAttack() const;
    bool isEnableBreak() const;

    void exeSign();
    void exeJut();
    void exeWait();
    void exeBreak();

    LiveActor* mOwner;
    ModelObj* mBreakModel;
    Quat4f mPoseQuat;
    Vec3f mGroundPos;
};
