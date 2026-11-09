#pragma once

#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/GroundChecker.hpp"
#include "Game/Util/JointController.hpp"

class PunchingKinoko : public LiveActor {
public:
    PunchingKinoko(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void initShadow();
    void initSensor();
    void initCamera(const TableIter& rIter);
    void initJointControl();

    bool requestPunch(HitSensor* pOtherSensor, HitSensor* pMySensor);
    bool requestEnemyBlow(HitSensor* pOtherSensor, HitSensor* pMySensor);
    bool requestTrample(HitSensor* pOtherSensor, HitSensor* pMySensor);
    bool requestCrush();

    void exeWait();
    void exeSwing();
    void exePointSnaped();
    void exePunched();
    void exePunchedBrake();
    void exeHitted();
    void exeCrushed();
    void exeCrushedEnd();

    bool ballMtxCallBack(Pos3f* pMtx, const JointControllerInfo& rJoint);

    bool isEnablePunched() const;
    bool isEnableHitPlayer() const;
    bool isEnableEnemyAttack() const;
    bool isEnableCrushed() const;
    bool isEnableTrample() const;
    bool isEnableBlowed() const;
    bool isCrushed() const;

    void addVelocityKeepHeight();
    Vec3f addSpringToRest();

private:
    GroundChecker* mGroundChecker;
    AnimScaleController* mScaleController;
    JointControlDelegator< PunchingKinoko >* mDelegator;
    s32 mCameraVecId;  // Obj_arg7
    Vec3f mCameraTarget;
    Vec3f mShadowNormal;  // the squash axis: the ground's normal under the head when crushed
    s32 mStarPointerHitCoolDown;
    bool mIsPointerHeld;  // 2P button held since before pointing, or already used
    s32 mInvincibleHitCoolDown;
};
