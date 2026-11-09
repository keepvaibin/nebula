#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class Gesso : public LiveActor {
public:
    Gesso(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;

    void selectNextNerve();
    void knockOut(HitSensor*, HitSensor*);
    bool clipAndInitPos();
    bool calcWalkMove(s32);
    bool calcSinkMove(s32);
    // Speeds along the side, up and front vectors.
    void calcAndSetVelocity(f32 side, f32 up, f32 front);
    // A zero vector targets +Y.
    void calcAndSetUpVecTarget(f32 x, f32 y, f32 z);
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
