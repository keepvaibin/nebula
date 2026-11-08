#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class SkeletalFishBoss;

class SkeletalFishGuard : public LiveActor {
public:
    SkeletalFishGuard(SkeletalFishBoss*, const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeAppear();
    void exeNormal();
    void exeApart();
    void exeFollow();
    void exeStraight();
    void exeDefence();
    void exeKill();
    void exeNumb();
    void endNumb();

    void appearNaturally();
    void appearForce();
    void killNaturally();
    void waitAttack(s32);
    void initSensor();
    void calcTransAndFront();
    void rotateHorizontal(const Vec3f&, f32);
    void rotateVertical(const Vec3f&, f32);
    bool tryShiftApart();
    bool tryShiftStraight();
    bool tryShiftKill();
    void turn(Vec3f*, const Vec3f&, const Vec3f&, f32);
    void lookToPlayer(f32, f32);
    void calcTarget(Vec3f*, Vec3f*, Vec3f*, s32);
    bool isInScreen() const;
    bool isPlayerInAttackRange() const;
    bool isLineOfSightClear() const;
    bool tryShiftNumb(const Nerve*);

    SkeletalFishBoss* mFishBoss;
    Vec3f mGuardOffset;
    u32 mAppearWaitStep;
    Vec3f mApartVelocity;
    f32 mApartFallSpeed;
    s32 mAttackDelay;
    f32 mSpeed;
    const Nerve* mNumbReturnNerve;
    Vec3f mFront;
    Vec3f mAppearRandomDir;
    Vec3f mTargetRailPos;
    Vec3f mTargetPos;
    Vec3f mTargetRailDir;
    Vec3f mAppearStartPos;
    AnimScaleController* mScaleController;
};
