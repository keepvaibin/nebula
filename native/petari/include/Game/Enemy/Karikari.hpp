#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FixedPosition;

class Karikari : public LiveActor {
public:
    Karikari(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void applyEnvironmentInfluenceToVelocity();
    void killedInFrozenState();

    void exeFall();
    void exeLand();
    void exeNoCalcWait();
    void exeWait();
    void exePrePursue();
    void exePursue();
    void exeWatchFor();
    void exePreCling();
    void exeCling();
    void exeRelease();
    void exeSpinAttacked();
    void exePointerAttacked();
    void exeFrozen();
    void exeFrozenRecover();
    void exeBlowOut();
    void exeInTornado();
    void exePress();

    bool tryBlowOut(const Vec3f&, bool);
    bool tryElectricKill();
    bool tryHipDropRelease();
    bool tryPointerRelease(const Vec2f&);
    bool tryPointerAttacked();

    void setVelocityFromCursorMove(const Vec2f&);
    void generateItem(s32 numStarPieces);

    void tryTurnToDirection(const Vec3f& rDir, f32 turnRatio);

    bool inClingNerve() const;

    Vec3f mFront;
    FixedPosition* mClingPosition;
    bool mIsReadyToLandTornado;
    bool mCanAttack;
    bool mIsPushable;
    s32 mFrozenTime;
};
