#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpinHitController;
class AnimScaleController;
class AnimStampController;
class StringSpiderString;
class FixedPosition;

class StringSpider : public LiveActor {
public:
    StringSpider(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void calcAnim() override;
    virtual void kill() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    s32 getSearchToAttackTime();
    void doSwingAction(f32 accel, bool isSideSwing);
    void reactPointer();
    void reactStringForce();
    bool isOverStringLength(Vec3f* pToRoot, f32* pDistance);
    Vec3f calcToRootVector();
    void calcPosture(f32 upRate, bool isFacePlayer);
    void tearDownPointerGripped();
    void extendString();
    void shortenString(f32 length);
    bool reactSpinHit(HitSensor* pSender, HitSensor* pReceiver);
    void startSwingLevelSound();

    void exeWait();
    void endWait();
    void exeDownStart();
    void exeDownEnd();
    void exeDown();
    void exeUp();
    void exeSearch();
    void exeAttack();
    void exeHit();
    void exeSpinned();
    void exeTrampleFall();
    void exeWallHitDeath();
    void exeBlow();
    void exePointerGripped();
    void endPointerGripped();

    SpinHitController* mSpinHitController;
    AnimScaleController* mScaleController;
    AnimStampController* mStampController;
    StringSpiderString* mString;
    FixedPosition* mFixedPos;
    Quat4f mPoseQuat;
    f32 mStringLengthMax;
    f32 mStringLength;
    bool mIsPointerPulled;
    s32 mPointerStep;
    Vec3f* mSwingFront;  // set only on a fixed swing line (Obj_arg1 == 2)
    Vec3f* mSwingSide;
};

class StringSpiderString : public LiveActor {
public:
    StringSpiderString(const char* pName);

    virtual void calcAndSetBaseMtx() override;

    Quat4f mRotateQuat;
    Vec3f mRootPos;
};
