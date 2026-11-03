#pragma once

#include "Game/Enemy/WaterBazookaCapsule.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class CameraTargetDemoActor;
class CollisionParts;
class ElectricPressureBullet;
class JointController;
struct JointControllerInfo;
class ModelObj;
class MogucchiShooter;

class WaterBazooka : public LiveActor {
public:
    WaterBazooka(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void kill();
    virtual void makeActorDead();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    bool isFirstShoot() const;
    bool isTired() const;
    bool isPanic() const;
    bool isBazookaPinch() const;
    bool isBazookaLifeOut() const;

    void exeWaitForBattle();
    void exeWait();
    void exeAim();
    void exeAimEnd();
    void exeShot();
    void exeShotNoMotion();
    void exeTire();
    void exeDemoCrackCapsule();
    void exeDemoAnger();
    void exeDemoBreakWait();
    void exeDemoBreakSign();
    void exeDemoBreakExplosion();
    void exeDemoBreakEnd();
    void exeWaitForLaugh();
    void exePanic();
    void exeStorm();
    void endStorm();

    void startDemoCrackCapsule();
    void startDemoAnger();
    void startDemoBreakCapsule();

    bool aimAtMario();
    void switchShowOrHide();
    void updateElectricLeak();
    void damageBazooka(HitSensor*, HitSensor*);
    bool calcJointCannon(TPos3f*, const JointControllerInfo&);
    s32 getSmokeLevel() const;  // enum?

    bool tryWaitForBattle();
    bool tryPanic();
    bool isNrvDemo() const;
    bool isElectricLeakSign() const;
    bool isElectricLeak() const;

    void initShooter();
    void initBazookaCapsule();
    void initBreakModel();
    void initCameraTarget();
    void initBullet(const JMapInfoIter&);
    bool tryShotBullet();
    ElectricPressureBullet* selectBulletElectric();
    bool tryJumpBackPlayerFromBazooka() const;
    void calcNearDropPoint(TVec3f*) const;
    void calcGunPointFromCannon(TPos3f*);
    void setCameraTargetMtx();

    inline void startBrk(const char* pName) {
        MR::startBrk(this, pName);
        MR::startBrk(mCapsule, pName);
    }

    MogucchiShooter* mShooter;
    WaterBazookaCapsule* mCapsule;
    ModelObj* mBreakModel;
    CameraTargetDemoActor* mDemoActor;
    ActorCameraInfo* mCameraInfo;
    s32 mAlreadyDoneFlag;
    s32 mLife;
    s32 mShotNum;
    CollisionParts* mCannonCollisionParts;
    JointController* mJointCtrl;
    TPos3f mBaseMtx;
    bool mIsPinchBGMStarted;
    bool mHasPowerStar;
    bool mIsElectric;
    ElectricPressureBullet** mBullets;
    s32 mElectricTime;
};
