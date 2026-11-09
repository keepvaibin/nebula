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

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;

    bool isRequestAttackVs1() const;
    s32 calcRemainTimeToBreak() const;
    bool isRequestAttackVs3() const;
    f32 calcAndSetTargetPos(Vec3f* pPos, const Vec3f& rReferencePos);
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
    // Obj_arg1: the landing Koopa fires at this stair from.
    s32 mLevel;
    // Obj_arg5: the edge point (offset table index) fire aims at; -1 aims at the nearest.
    s32 mTargetCorner;
    // Obj_arg6: Koopa jumps on once this stair's attack is answered.
    bool mIsJumpAfterAttack;
    s32 mType;   // Obj_arg2
    bool mIsBig;
    bool mIsTurn;
    bool mIsKoopaAttack;
    Vec3f mTargetPos;
    s32 mWaitFallStep;  // Obj_arg3
    s32 mFallStep;      // Obj_arg4
};
