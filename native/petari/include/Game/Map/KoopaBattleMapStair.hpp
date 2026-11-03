#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KoopaBattleMapStair : public LiveActor {
public:
    enum Type {
        Type_Normal,
        Type_NoRequestFire,
        Type_DemoFar,
        Type_DemoNear,
    };

    KoopaBattleMapStair(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();

    bool isRequestAttackVs1() const;
    s32 calcRemainTimeToBreak() const;
    bool isRequestAttackVs3() const;
    f32 calcAndSetTargetPos(TVec3f* pPos, const TVec3f& rReferencePos);
    f32 calcTimeRate() const;
    bool isBreak() const;
    bool isTypeNormal() const;
    bool isTypeDemoFar() const;
    bool isTypeDemoNear() const;
    bool isTypeNoRequestFire() const;

    void exeWaitSwitch();
    void exeWaitKoopaFire();
    void exeWaitFall();
    void exeFall();
    void exeDisappear();

    s32 mTimeToBreak;  // Obj_arg0
    s32 mFireAttackStep;
    s32 mArg1;   // Obj_arg1
    s32 mArg5;   // Obj_arg5
    bool mArg6;  // Obj_arg6
    s32 mType;   // Obj_arg2
    bool mIsBig;
    bool mIsTurn;
    bool _A6;
    s32 _A8;
    TVec3f _AC;
    s32 mWaitFallStep;  // Obj_arg3
    s32 mFallStep;      // Obj_arg4
};
