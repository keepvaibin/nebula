#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class AnimScaleController;
class FootPrint;

class Petari : public LiveActor {
public:
    Petari(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void calcAnim() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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
    void moveTowardTargetDirection(f32 speed, f32 damp, f32 turnDegree);
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
    Mtx34f mHeadMtx;
};
