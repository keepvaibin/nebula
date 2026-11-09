#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class SkeletalFishBoss;

class SkeletalFishGuard : public LiveActor {
public:
    SkeletalFishGuard(SkeletalFishBoss* pFishBoss, const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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
    void waitAttack(s32 steps);
    void initSensor();
    void calcTransAndFront();
    void rotateHorizontal(const Vec3f& rDir, f32 maxRadian);
    void rotateVertical(const Vec3f& rDir, f32 maxRadian);
    bool tryShiftApart();
    bool tryShiftStraight();
    bool tryShiftKill();
    void turn(Vec3f* pVec, const Vec3f& rFrom, const Vec3f& rTo, f32 maxRadian);
    void lookToPlayer(f32 yawRadian, f32 pitchRadian);
    void calcTarget(Vec3f* pRailPos, Vec3f* pPos, Vec3f* pRailDir, s32 stepsAhead);
    bool isInScreen() const;
    bool isPlayerInAttackRange() const;
    bool isLineOfSightClear() const;
    bool tryShiftNumb(const Nerve* pReturnNerve);

    SkeletalFishBoss* mFishBoss;
    Vec3f mGuardOffset;     // slot by the boss: x side, y up, z along the rail
    u32 mAppearWaitStep;    // steps in Wait before the appear flight
    Vec3f mApartVelocity;   // last step's move; the launch velocity of Apart
    f32 mApartFallSpeed;
    s32 mAttackDelay;       // steps before Apart is allowed
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
