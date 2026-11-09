#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class JellyfishElectric : public LiveActor {
public:
    JellyfishElectric(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeWaitWithLeftTurn();
    void exeWaitWithRightTurn();
    void exeDamage();
    void exeDeath();
    void exeAttack();
    void exeRailGoal();
    void exePointerSwoon();
    void endPointerSwoon();
    void waitTurn();
    void knockOut();
    bool tryToAttackElectric(HitSensor*, HitSensor*);
    bool tryPointerSwoon();
    bool selectNerveAfterWait();

    AnimScaleController* mController;
    WalkerStateBindStarPointer* mBindStarPtr;
    s32 mWaveStep;
    Vec3f mFront;
    bool mIsDanger;
    bool mIsConnectedRail;
    f32 mRailSpeed;
    s32 mRailGoalWaitStep;
    bool mIsRailTurnBack;
    Vec3f mClipCenter;
};
