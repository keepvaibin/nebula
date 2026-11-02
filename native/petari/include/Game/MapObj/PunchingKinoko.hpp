#pragma once

#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Map/GroundChecker.hpp"
#include "Game/Util/JointController.hpp"

class PunchingKinoko : public LiveActor {
public:
    PunchingKinoko(const char*);

    virtual ~PunchingKinoko();
    virtual void init(const JMapInfoIter&);
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void makeActorDead();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

    void initShadow();
    void initSensor();
    void initCamera(const JMapInfoIter&);
    void initJointControl();

    bool requestPunch(HitSensor*, HitSensor*);
    bool requestEnemyBlow(HitSensor*, HitSensor*);
    bool requestTrample(HitSensor*, HitSensor*);
    bool requestCrush();

    void exeWait();
    void exeSwing();
    void exePointSnaped();
    void exePunched();
    void exePunchedBrake();
    void exeHitted();
    void exeCrushed();
    void exeCrushedEnd();

    bool ballMtxCallBack(TPos3f*, const JointControllerInfo&);

    bool isEnablePunched() const;
    bool isEnableHitPlayer() const;
    bool isEnableEnemyAttack() const;
    bool isEnableCrushed() const;
    bool isEnableTrample() const;
    bool isEnableBlowed() const;
    bool isCrushed() const;

    void addVelocityKeepHeight();

private:
    GroundChecker* mGroundChecker;
    AnimScaleController* mScaleController;
    JointControlDelegator< PunchingKinoko >* mDelegator;
    s32 _98;
    TVec3f _9C;
    TVec3f _A8;
    s32 mStarPointerHitCoolDown;
    bool _B8;
    s32 mInvincibleHitCoolDown;
};
