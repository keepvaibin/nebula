#pragma once

#include "Game/Enemy/WaterBazookaCapsule.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include <nebula_geometry.hpp>

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

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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
    bool calcJointCannon(Pos3f*, const JointControllerInfo&);
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
    void initBullet(const TableIter&);
    bool tryShotBullet();
    ElectricPressureBullet* selectBulletElectric();
    bool tryJumpBackPlayerFromBazooka() const;
    void calcNearDropPoint(Vec3f*) const;
    void calcGunPointFromCannon(Pos3f*);
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
    Pos3f mBaseMtx;
    bool mIsPinchBGMStarted;
    bool mHasPowerStar;
    bool mIsElectric;
    ElectricPressureBullet** mBullets;
    s32 mElectricTime;
};
