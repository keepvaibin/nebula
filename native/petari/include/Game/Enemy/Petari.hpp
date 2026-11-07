#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class AnimScaleController;
class FootPrint;

class Petari : public LiveActor {
public:
    Petari(const char* pName);

    virtual void init(const TableIter& rIter);
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
    void initFootPrint(const TableIter& rIter);
    void updateFootPrint();
    void initFrontVec();
    void initStarPiece(const TableIter& rIter);
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
    void calcMouthPos(Vec3f* pOut) const;

    AnimScaleController* mAnimScaleCtrl;
    FootPrint* mFootPrint;
    f32 mFootprintYOffs;
    bool mRequestSmoke;
    Vec3f mFront;
    Vec3f mBodyCenter;
    Vec3f mTargetDir;
    s32 mMeanderStep;
    f32 mMeanderAngle;
    s32 mStarPieceTotal;
    s32 mStarPieceRemaining;
    u8 _D0;  // unused
    Mtx34f mHeadMtx;
};
