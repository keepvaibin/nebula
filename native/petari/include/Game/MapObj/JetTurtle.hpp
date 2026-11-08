#pragma once

#include "Game/LiveActor/LiveActor.hpp"

#define JETTURTLETYPE_RED 1
#define JETTURTLETYPE_GOLD 2

class JetTurtle : public LiveActor {
public:
    JetTurtle(const char*);

    virtual ~JetTurtle();
    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgThrow(HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);
    virtual void resetPosition();
    virtual void reset(u32);

    void exeWait();
    void endWait();
    void exeWait2();
    void endWait2();
    void exeThrowWait();
    virtual void exeThrowing();
    void exeTakenReserve();
    void exeTakenStart();
    void exeRestart();
    void exeDrop();

    void init2(const TableIter&, s32);
    void becomeSlowType();
    void becomeFastType();
    void appearAndTryTaken();
    void resetPositionAndVanish();
    void boundDrop();
    void bound();
    bool isWait() const;
    bool isRestart() const;

    inline bool isNerveWait();

    f32 mThrowSpeedBonus;
    u16 mShellType;  // (0 = green, 1 = red, 2 = gold)
    u16 mType;
    HitSensor* mTakenSensor;
    HitSensor* mHomingTarget;
    Vec3f mThrowDir;
    Vec3f mInitPos;
    Vec3f mInitRotation;
    Vec3f mThrowStartPos;
    Vec3f mLastGravity;
    u16 mSpinTime;
    u8 mIsRevive;
    u8 mIsAppeared;
    u8 mHasReviveLight;
    u8 mIsResetRequested;
};

class GoldenTurtle : public JetTurtle {
public:
    GoldenTurtle(const char* pName) : JetTurtle(pName) {
    }

    virtual ~GoldenTurtle();
    virtual void init(const TableIter&);
    virtual void resetPosition();
    virtual void reset(u32);
    virtual void exeThrowing();
};
