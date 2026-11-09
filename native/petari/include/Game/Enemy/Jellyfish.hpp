#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class Jellyfish : public LiveActor {
public:
    Jellyfish(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeFind();
    void exeThreat();
    void exeDeath();
    void exeAttack();
    void exeRailGoal();
    void exePointerSwoon();
    void exeWaitWithLeftTurn();
    void exeWaitWithRightTurn();
    void exeThreatWithLeftTurn();
    void exeThreatWithRightTurn();

    void threatTurn();
    void faceToMario();
    void knockOut(HitSensor*, HitSensor*);
    bool selectNerveAfterWait();
    bool selectNerveThreat();
    bool tryPointerSwoon();

    AnimScaleController* mController;
    WalkerStateBindStarPointer* mBindStarPtr;
    s32 mWaveStep;
    Vec3f mFront;
    bool mIsConnectedRail;
    f32 mRailSpeed;
    s32 mRailGoalWaitStep;
    bool mIsRailTurnBack;
    Vec3f mClipCenter;
};
