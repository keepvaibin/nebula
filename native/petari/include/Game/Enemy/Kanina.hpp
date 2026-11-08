#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimationRandomPlayer;
class BenefitItemObj;
class JointRumbler;

class Kanina : public LiveActor {
public:
    enum KaninaType {
        KaninaType_Blue = 0,
        KaninaType_Red = 1,
    };

    enum SeVersion {
        SeVersion_NORMAL = 0,
        SeVersion_WATER = 1,
    };

    Kanina(const char*);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void appearItemPunch();
    void appearItemTrample();
    bool tryAttack(HitSensor*, HitSensor*);
    bool tryPushEach(HitSensor*, HitSensor*);
    bool receiveTrample(HitSensor*, HitSensor*);
    bool receivePunch(HitSensor*, HitSensor*);
    bool receiveFireBall(HitSensor*, HitSensor*);
    void doDamageFireBall(HitSensor*, HitSensor*);
    void startRun();
    void startRunAwayLevelSound();
    bool receiveHipDrop(HitSensor*, HitSensor*);
    bool receiveInvincibleAttack(HitSensor*, HitSensor*);
    bool isStateStayOnGround() const;
    void initForType(const TableIter&, KaninaType);
    bool isPlayerBackward(f32) const;
    bool isStatePossibleToAttack() const;
    bool isStateBlink() const;
    bool isStateGuard() const;
    bool isStateDamageNoFireBall() const;
    bool isBindedGroundWaterBottom() const;
    void updateMovement();
    bool tryFindPlayer();
    bool tryHitWall();
    bool tryTurn();
    bool tryPointing();

    void exeAppear();
    void exeWait();
    void exeAttack();
    void exeVauntAttackSuccess();
    void exeGuard();
    void exeGuardEnd();
    void exeDamageFireBall();
    void exeDamageHipDrop();
    void exeDig();
    void exeWaitUnderGround();
    void exeFindPlayer();
    void exeRunAwayBreak();
    void exeReboundEach();
    void exeTurn();
    void exeTurnEnd();
    void exePointing();
    void endPointing();
    void exeWalk();
    void exeRunAway();
    void exeRunAwayReboundDirection();
    void exeHitWall();

    inline f32 getFloatDependentType(f32 fRed, f32 fBlue) {
        return mType == KaninaType_Red ? fRed : fBlue;
    }

    JointRumbler* mJointRumbler;
    BenefitItemObj* mKinokoOneUp;
    AnimationRandomPlayer* mAnimationRandomPlayer;
    Vec3f mInitPos;
    Quat4f mPoseQuat;
    u32 mRunAwayStep;
    u32 mRunAwayBreakStep;
    f32 mRunAwaySide;
    Vec3f mHitWallDir;
    KaninaType mType;
    Vec3f mFront;
    s32 mWalkCount;
};
