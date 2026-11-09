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

    BossBegoman(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void calcAnim() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual u32 getKind() const override {
        return Kind_Boss;
    }
    virtual bool onTouchElectric(const Vec3f& rRailPos, const Vec3f& rRailDir) override;
    virtual bool setNerveReturn() override;
    virtual const Nerve* getNerveWait() override;
    virtual void addVelocityOnPushedFromElectricRail(const Vec3f& rRailPos, const Vec3f& rRailDir) override;
    virtual bool requestAttack() override;

    bool isNerveOnWeak() const;
    bool isNerveElectricOrJump() const;
    // Blow, the electric hit and its return, and the jump home: attacks and messages are ignored.
    bool isNerveIgnoreAttack() const;
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
    void exeBrake();
    void exeStepBack();
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
    void recoverEdge();

    void tryLaunchFollower();
    void killAllFollower(FollowerKind kind);

    bool isDeadAllFollower();

    bool receiveMsgTrample(HitSensor* pSender, HitSensor* pReceiver);

    void startRotationLevelSound();

    BegomanBaby** mBabyFollowers;
    BegomanSpike** mSpikeFollowers;
    s32 mBabyFollowerNum;
    s32 mSpikeFollowerNum;
    FollowerKind mFollowerKind;
    ParabolicPath* mPath;
    BossBegomanHead* mHead;
    Pos3f mHeadMtx;
    s32 mHealth;
    f32 mPursueSpeedRate;
    ActorCameraInfo* mOpeningDemoInfo;
};
