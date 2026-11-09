#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class AnimStampController;
class ModelObj;
class SpinHitController;

class BasaBasa : public LiveActor {
public:
    BasaBasa(const char* pName);

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

    void exeWait();
    void exeAirWait();
    void exeChaseStart();
    void exeChase();
    void exeQuickTurnStart();
    void exeQuickTurn();
    void exeTrampleDown();
    void exePunchDown();
    void exeAttackStart();
    void exeAttack();
    void exeAttackEnd();
    void exeAttackEndRecover();
    void exeHitBack();
    void exeHitBackEnd();
    void exeComeHome();
    void exeAttachCelling();
    void exePointerSwoon();
    void endPointerSwoon();
    void exeStun();
    void initHangModel();
    bool tryClippingAndResetPos();
    bool trySetNervePointerSwoon();
    bool tryComeHome();
    void updateRailType();
    void controlVelocity();
    void tuneHeight();
    bool isNearTarget(f32 distance) const;
    bool isDown() const;
    bool isNrvEnableStun() const;

    ModelObj* mHangModel;
    AnimScaleController* mScaleController;
    AnimStampController* mStampController;
    SpinHitController* mSpinHitController;
    Vec3f mFront;
    f32 mChaseDistance;   // Obj_arg0
    f32 mAttackDistance;  // random 600..800
    const Vec3f* mTarget;  // the player's center, the rail position or mHomePos
    f32 mChaseTurnAngle;  // random -25..25 degrees off the target
    Vec3f mAttackTarget;
    bool mIsIceModel;
    Vec3f mHomePos;
    Vec3f mHomeFront;
    f32 mHomeDistance;  // Obj_arg1, default mChaseDistance + 1000
    s32 mClipStep;
    bool mIsOnCeiling;
};
