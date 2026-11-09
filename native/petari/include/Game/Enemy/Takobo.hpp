#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpinHitController;
class AnimScaleController;
class SpinningBox;

class Takobo : public LiveActor {
public:
    Takobo(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initSensor();
    void generateCoin();
    bool tryPress();
    void exeMove();
    void endMove();
    void exeWait();
    void endWait();
    void exePress();
    void exeHitPunch();
    void exeStunEnd();
    void exeHitReaction();
    void exeAttack();
    void endAttack();
    void exeStunStart();
    void exeStun();
    void exeIce();
    void endPointerAimed();
    void exePointerAimed();

    Vec3f mFront;
    bool mIsCheckFireGround;
    Vec3f mInitPos;
    Vec3f mMoveDir;
    bool mIsMoveBack;
    f32 mMoveSpeed;
    f32 mMoveRange;
    f32 mMoveStart;
    f32 mMoveEnd;
    s32 mMoveStep;
    SpinningBox* mBox;
    AnimScaleController* mScaleController;
    SpinHitController* mSpinController;
};
