#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class Jellyfish : public LiveActor {
public:
    Jellyfish(const char*);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

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
    bool faceToMario();
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
