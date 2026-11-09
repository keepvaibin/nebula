#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class JetTurtle : public LiveActor {
public:
    enum ShellType {
        SHELL_GREEN,  // slow, never homes
        SHELL_RED,
        SHELL_GOLD,
    };

    JetTurtle(const char*);

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
    void exeThrowWait();
    virtual void exeThrowing();
    void exeTakenReserve();
    void exeTakenStart();
    void exeRestart();
    void exeDrop();

    void init2(const TableIter& rIter, bool isGolden);
    void becomeSlowType();
    void becomeFastType();
    void appearAndTryTaken();
    void resetPositionAndVanish();
    void boundDrop();
    void bound();
    bool isWait() const;
    bool isRestart() const;

    f32 mThrowSpeedBonus;
    u16 mShellType;  // ShellType
    u16 mSpeedType;  // index of the speed tables; 0 also adds the player's speed to a throw
    HitSensor* mTakenSensor;
    HitSensor* mHomingTarget;
    Vec3f mThrowDir;
    Vec3f mInitPos;
    Vec3f mInitRotation;
    Vec3f mThrowStartPos;
    Vec3f mLastGravity;
    bool mIsRevive;
    bool mIsAppeared;  // placed visible rather than by a switch
    bool mHasReviveLight;
    bool mIsResetRequested;
};

class GoldenTurtle : public JetTurtle {
public:
    GoldenTurtle(const char* pName) : JetTurtle(pName) {
    }

    virtual void init(const TableIter&);
    virtual void resetPosition();
    virtual void reset(u32);
    virtual void exeThrowing();
};
