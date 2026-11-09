#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class HitSensor;
class WalkerStateBindStarPointer;

class TakoHei : public LiveActor {
public:
    TakoHei(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void initSensor();

    bool requestPressDown();
    bool requestHipDropDown();
    bool requestFlatDown();
    bool requestSwoon();
    bool requestPunch(HitSensor*, HitSensor*);
    bool requestAttackSuccess(HitSensor*, HitSensor*);

    bool tryActive();
    bool tryNonActive();
    bool tryWalk();
    bool tryWalkEnd();
    bool tryFindTurn();
    bool tryFind();
    bool tryPursue();
    bool tryPursueEnd();
    bool tryAttack();
    bool tryCoolDownEnd();
    bool tryAttackSuccessEnd();
    bool tryPointBind();
    bool tryRecover();
    bool tryRecoverEnd();
    bool tryPressed();

    void exeNonActive();
    void exeWait();
    void exeWalk();
    void exeFindTurn();
    void exeFind();
    void exePursue();
    void exeCoolDown();
    void exeAttackSign();
    void exeAttack();
    void exeAttackSuccess();
    void exeBindStarPointer();
    void endBindStarPointer();
    void exeSwoon();
    void exeRecover();
    void exePressDown();
    void exeHipDropDown();
    void exeFlatDown();
    void exePunchDown();

    void updatePose();
    void updateNormalVelocity();
    void updateSwoonVelocity();
    void decideNextTargetPos();

    bool checkFront() const;
    bool canAttack() const;
    bool isPushMovable() const;
    bool isEnableKick() const;
    bool isDown() const;
    bool isFallNextMove() const;
    bool isInSightMario() const;

    AnimScaleController* mScaleCtrl;
    WalkerStateBindStarPointer* mBindStarPointer;
    Quat4f mPoseQuat;
    Vec3f mFront;
    Vec3f mWalkTarget;
    Vec3f mInitPos;
    u32 mDeathItem;
    s32 mNoPushStep;
    u8 mHasMoveLimit;
};
