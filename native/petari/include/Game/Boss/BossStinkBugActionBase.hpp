#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include <nebula_geometry.hpp>

class BossStinkBug;
class HitSensor;
class SpringValue;

class BossStinkBugActionBase : public ActorStateBase< BossStinkBug > {
public:
    BossStinkBugActionBase(const char*, BossStinkBug*);

    virtual bool isValidFollowId(s32 /*id*/) const {
        return false;
    };
    virtual void attackSensor(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {};
    virtual bool receiveMsgPlayerAttack(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    };
    virtual bool receiveOtherMsg(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
        return false;
    };
    virtual bool isAngry() const {
        return false;
    };

    bool updateGroundWalk(f32 speed);
    bool updateFlyRoll();
    bool updateFlyRollWait(s32 waitSteps);
    bool updateFlyRollEnd();
    bool updateFlyDash(f32 startSpeed);
    void throwBomb(s32 newWaitTime);
    void resetThrowBombTiming();
    f32 calcShakeOffSignSpeed(s32 numSteps) const;
    f32 calcShakeOffSpeed() const;
    f32 calcShakeOffEndSpeed() const;
    s32 calcKeepRollTime(s32 waitTime) const;
    s32 calcFlySoundVol() const;
    s32 calcIIRCutOff() const;
    void updateSound();
    void updateSoundFly();
    bool updateGroundRegainRail(f32 speed);
    bool updateGroundFind();
    bool updateGroundShakeOff();
    bool updateGroundRun(f32 maxSpeed);
    bool updateGroundAttackSuccess();
    bool updateGroundCoolDown();
    bool updateGroundTurn();
    bool updateGroundDamage();
    bool updateFly();
    bool updateFlyShakeOffSign(s32);
    bool updateFlyShakeOff();
    bool updateFlyShakeOffWait(s32);
    bool updateFlyShakeOffEnd();
    bool updateFlyRollSign(s32);
    bool updateFlyTurnSign();
    bool updateFlyTurn();
    bool updateFall();
    bool updateFallDamage();
    bool updateToGround();
    bool updateLand();
    bool updateToFly();
    void startSoundFly(s32);

    SpringValue* mSpringValue;
    Vec3f mRailDiff;
    Vec3f mTurnFront;
    f32 mLandingHeight;
    f32 mToFlyStartSpeed;
    f32 mLandingHeightSpeed;
    f32 mFlySpeed;
    f32 mHeightOffset;
    f32 mLocalHeightOffset;
    f32 mSpeed;
    u32 mThrowBombTimer;
    bool mIsRailToEnd;
    f32 mCameraDistance;
    f32 mPrevCameraDistance;
    s32 mApproachStep;
};
