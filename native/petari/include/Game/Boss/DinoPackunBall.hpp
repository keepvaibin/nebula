#pragma once

#include "Game/Boss/DinoPackunTailNode.hpp"

class DinoPackunBall : public DinoPackunTailNode {
public:
    DinoPackunBall(const char*, DinoPackun*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual void requestLockPosition() override;
    virtual void requestUnLockPosition() override;
    virtual void addNodeVelocity(const Vec3f&) override;
    virtual JointController* createJointControllerOwn(LiveActor*, const char*) override;

    void setWeakSensor(HitSensor*);
    void addDodgeTargetVelocity();
    bool requestPunch(HitSensor*, HitSensor*);
    void setDamageFire();
    void setDamageNone();
    bool tryRebound();
    const char* getDragEffectName() const;

    void exeWait();
    void exeShoot();
    void exeCharge();
    void exeReverse();
    void exeRebound();
    void exeLock();

    Pos3f mDragEffectMtx;
    HitSensor* mWeakSensor;
    Vec3f mReboundVelocity;
    bool mIsBlackDrag;
    bool mIsOnFire;
    s32 mDragSoundStep;
};
