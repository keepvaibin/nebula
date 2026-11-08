#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class Gesso : public LiveActor {
public:
    Gesso(const char*);

    virtual ~Gesso();
    virtual void init(const TableIter&);
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

    void selectNextNerve();
    void knockOut(HitSensor*, HitSensor*);
    bool clipAndInitPos();
    bool calcWalkMove(s32);
    bool calcSinkMove(s32);
    void calcAndSetVelocity(f32, f32, f32);
    void calcAndSetUpVecTarget(f32, f32, f32);
    bool isMarioUp() const;
    bool isMarioLeft() const;
    bool tryChangeHighSpeedMode();
    bool tryPointerSwoon();

    void exeComeFromBox();
    void exeWait();
    void exeSearch();
    void exeWalkCharge();
    void exeWalk();
    void exeSink();
    void exeLostPlayer();
    void exeAttack();
    void exePunchDown();
    void exeComeBack();
    void exePointerSwoon();
    void endPointerSwoon();
    void exeRotate();

private:
    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mStateBindStarPointer;
    Vec3f mInitPos;
    Vec3f mFront;
    Vec3f mUp;
    Vec3f mUpTarget;
    s32 mClipStep;
    Vec3f mPushVelocity;
    bool mIsMarioLeft;
    bool mIsHighSpeedMode;
};
