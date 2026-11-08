#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class AnimStampController;
class ModelObj;
class SpinHitController;

class BasaBasa : public LiveActor {
public:
    BasaBasa(const char*);

    virtual ~BasaBasa();
    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
    bool isNearTarget(f32) const;
    bool isNrvEnableStun() const;

    ModelObj* mHangModel;
    AnimScaleController* mScaleController;
    AnimStampController* mStampController;
    SpinHitController* mSpinHitController;
    Vec3f mFront;
    f32 mChaseDistance;
    f32 mAttackDistance;
    const Vec3f* mTarget;
    f32 mChaseTurnAngle;
    Vec3f mAttackTarget;
    bool mIsIceModel;
    Vec3f mHomePos;
    Vec3f mHomeFront;
    f32 mHomeDistance;
    s32 mClipStep;
    u8 mIsOnCeiling;
};
