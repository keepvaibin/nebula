#pragma once

#include "Game/Enemy/BegomanBase.hpp"

class ActorCameraInfo;
class BegomanBaby;
class BegomanSpike;
class BossBegomanHead;
class ParabolicPath;

class BossBegoman : public BegomanBase {
public:
    enum FollowerKind {
        FollowerKind_BabyFollower,
        FollowerKind_SpikeFollower,
        FollowerKind_BothFollower,
    };

    BossBegoman(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void calcAnim();
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual u32 getKind() const {
        return 0;
    };
    virtual bool onTouchElectric(const TVec3f&, const TVec3f&);
    virtual bool setNerveReturn();
    virtual const Nerve* getNerveWait();
    virtual void addVelocityOnPushedFromElectricRail(const TVec3f&, const TVec3f&);
    virtual bool requestAttack();

    void setStepBackNerve();

    void exePreDemoWait();
    void endPreDemoWait();
    void exeFirstContactDemo();

    void exeReady();
    void exeNoCalcWait();
    void endNoCalcWait();
    void exeWait();
    void exeSignAttack();
    void exePursue();
    void exeTurn();
    void endTurn();
    void exeOnWeak();
    void endOnWeak();
    void exeOnWeakTurn();
    void exeBrake();
    void exeStepBack();
    void exeStepBackOnWeak();
    void exeReturn();
    void exeProvoke();
    void exeTrampleReaction();
    void exeAware();
    void exeHitReaction();
    void exeBlow();
    void exeElectricDeath();
    void exeElectricReturn();
    void exeJumpToInitPos();
    void exeKeepDistance();

    void edgeRecoverCore();

    void tryLaunchFollower();
    void killAllFollower(FollowerKind);

    bool isDeadAllFollower();
    bool isDeadAllFollowerCore(BegomanBase**, s32);

    bool receiveMsgTrample(HitSensor* pSender, HitSensor* pReceiver);

    void startRotationLevelSound();

    BegomanBaby** mBabyFollowers;
    BegomanSpike** mSpikeFollowers;
    s32 mBabyFollowerNum;
    s32 mSpikeFollowerNum;
    FollowerKind mFollowerKind;
    ParabolicPath* mPath;
    BossBegomanHead* mHead;
    TPos3f mHeadMtx;
    s32 mHealth;
    f32 _150;
    ActorCameraInfo* mOpeningDemoInfo;
};
