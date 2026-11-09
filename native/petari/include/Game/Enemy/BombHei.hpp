#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class AnimScaleController;
class JointController;
struct JointControllerInfo;
class WalkerStateBindStarPointer;

class BombHei : public LiveActor {
public:
    BombHei(const char*);

    enum Type { BOMB_HEI = 0, BOMB = 1 };

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgThrow(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void setGroupClipping(const TableIter& rIter);
    void startCountdown();
    void endCountdown();

    void exeLaunch();
    void exeStarting();
    void exeWait();
    void exeWalk();
    void exePursue();
    void exePursueHit();
    void exeSpinHit();
    void endSpinHit();
    void exeTrample();
    void exePhysics();
    void exeStop();
    void exeTaken();
    void endTaken();
    void exeThrown();
    void endThrown();
    void exeExplode();
    void exeBindStarPointer();
    void endBindStarPointer();

    bool calcJoint(Pos3f*, const JointControllerInfo&);
    void startBoundSound();

    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mBindStarPointerState;
    Quat4f mRotQuat;
    Vec3f mFront;
    Vec3f mBinderOffset;
    Vec3f mTargetFront;
    s32 mCountdown;
    s32 mFuseTime;
    HitSensor* mCarrySensor;
    f32 mExplodeRadius;
    JointController* mBodyJoint;
    Type mType;
    bool mExplosionIgnoreCollision;
};

struct BombHeiParam {
    f32 mSpeedH;
    f32 mGravAccel;
    f32 mFriction;
    f32 mTurnRate;
};
