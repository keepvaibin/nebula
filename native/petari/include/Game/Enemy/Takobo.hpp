#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpinHitController;
class AnimScaleController;
class SpinningBox;

class Takobo : public LiveActor {
public:
    Takobo(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
    u8 mIsCheckFireGround;
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
