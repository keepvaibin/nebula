#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class AnimScaleController;
class AnimStampController;
class SpinHitController;
class WalkerStateBindStarPointer;
class JointController;
struct JointControllerInfo;

class KoteBug : public LiveActor {
public:
    KoteBug(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeNoCalcWait();
    void endNoCalcWait();
    void exeWait();
    void endWait();
    void exeSearch();
    void exeEscapeSearch();
    void exePreEscape();
    void exeFlyStart();
    void exeFlyPursue();
    void exeFlyPursueLast();
    void exeOverturnFall();
    void exeOverturn();
    void exePreRecover();
    void exeRecover();
    void exeHitToPlayer();
    void exeBlow();
    void exeStampDeath();
    void exeBindStarPointer();
    void endBindStarPointer();
    void setOverturnFall(HitSensor* pSender, HitSensor* pReceiver);
    bool calcJoint(Pos3f* pMtx, const JointControllerInfo& rInfo);

    AnimScaleController* mScaleController;
    AnimStampController* mStampController;
    SpinHitController* mSpinHitController;
    WalkerStateBindStarPointer* mStarPointer;
    Vec3f mFront;
    Quat4f mPosture;
    JointController* mJointController;
};
