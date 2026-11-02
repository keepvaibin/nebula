#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class AnimScaleController;
class FootPrint;

class Petari : public LiveActor {
public:
    Petari(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void calcAnim();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeLurk();
    void exeJumpOut();
    void exeWait();
    void exeApproach();
    void exeEscape();
    void exeSwoonStart();
    void exeSwoon();
    void exeSwoonEnd();
    void exeLand();
    void exeSmash();
    void exeSpinOut();
    void exeKickOut();
    void exeFreeze();

    void endApproach();
    void endWait();
    void endFreeze();
    void endEscape();

    void initHitSensors();
    void initFootPrint(const JMapInfoIter& rIter);
    void updateFootPrint();
    void initFrontVec();
    void initStarPiece(const JMapInfoIter& rIter);
    bool reflectStarPointer2P();
    bool tryShiftApproach();
    bool tryApproachEnd();
    bool tryShiftJumpOut();
    bool tryShiftEscape();
    void tryEmitFirstFootPrintEffect();
    void calcCenter();
    void calcApproachDirection();
    void calcEscapeDirection();
    void meander();
    void avoidPlayer();
    void avoidWall();
    f32 calcBoost(f32 baseSpeed, f32 boostMultiplier, f32 effectiveRange) const;
    void moveTowardTargetDirection(f32, f32, f32);
    bool tryEscapeEnd();
    void calcSpinOutVelocity(f32 speed);
    void appearStarPieceGradually();
    void appearStarPieceAll();
    bool isSolidBody() const;
    bool receivePlayerAttackAtBody(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    bool receivePlayerAttackAtSpin(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    void calcMouthPos(TVec3f* pOut) const;

    AnimScaleController* mAnimScaleCtrl;
    FootPrint* mFootPrint;
    f32 mFootprintYOffs;
    bool mRequestSmoke;
    TVec3f mFront;
    TVec3f mBodyCenter;
    TVec3f mTargetDir;
    s32 mMeanderStep;
    f32 mMeanderAngle;
    s32 mStarPieceTotal;
    s32 mStarPieceRemaining;
    u8 _D0;  // unused
    TMtx34f mHeadMtx;
};
