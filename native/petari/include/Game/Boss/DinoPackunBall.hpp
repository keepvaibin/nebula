#pragma once

#include "Game/Boss/DinoPackunTailNode.hpp"

class DinoPackunBall : public DinoPackunTailNode {
public:
    DinoPackunBall(const char*, DinoPackun*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual void requestLockPosition();
    virtual void requestUnLockPosition();
    virtual void addNodeVelocity(const Vec3f&);
    virtual JointController* createJointControllerOwn(LiveActor*, const char*);

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
