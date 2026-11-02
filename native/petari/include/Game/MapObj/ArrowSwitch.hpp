#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ArrowSwitch : public LiveActor {
public:
    enum SwitchType {
        SwitchType_QuarterPlus = 0,
        SwitchType_QuarterMinus = 1,
        SwitchType_HalfPlus = 2,
        SwitchType_HalfMinus = 3,
    };

    enum Direction {
        Direction_Up = 0,
        Direction_Right = 1,
        Direction_Down = 2,
        Direction_Left = 3,
    };

    /// @brief Creates a new `ArrowSwitch`.
    /// @param pName A pointer to the null-terminated name of the object.
    ArrowSwitch(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    bool requestPunch(HitSensor*, HitSensor*);
    void listenOnSwitch();
    void listenOffSwitch();
    void exeWait();
    void exeRotate();
    void exeLock();
    bool isPlusLimit() const;
    bool isMinusLimit() const;
    s32 getOneStep() const;

    f32 mRotAngle;
    s32 mRotYTargetIndex;
    f32 mRotSpeed;
    s32 mSwitchType;
    bool mLockAfterRotate;
    bool mIsRotPlus;
    bool _9E;
    bool mIsPunch;
};
