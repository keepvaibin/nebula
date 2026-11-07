#pragma once

#include "Game/Boss/SkeletalFishBossInfo.hpp"
#include "Game/LiveActor/PartsModel.hpp"

class CameraTargetMtx;
class JointController;
struct JointControllerInfo;
class ModelObj;
class SkeletalFishGuard;
class SkeletalFishGuardHolder;
class SkeletalFishBossScarFlash;
class SkeletalFishBossHead;
class SkeletalFishRailControl;
class SkeletalFishBossBattleDirector;
class SkeletalFishBossRail;

class SkeletalFishBoss : public LiveActor {
public:
    struct SensorToCollider {
        const char* mJointName;
        const char* mColliderName;
    };

    struct JointToShadow {
        const char* mName;
        const char* mJointName;
        const char* mShadowName;
    };

    struct HitPos {
        const char* mName;
        const char* mSensorName;
        Vec mOffset;
    };

    typedef void (SkeletalFishBoss::*SceneFunc)(void);

    SkeletalFishBoss(const char*);

    virtual ~SkeletalFishBoss() {
    }

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void calcAnim();
    virtual void appear();
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();

    bool calcJoint(Pos3f*, const JointControllerInfo&);
    void damage(const HitSensor*, const Vec3f&);
    void exeSwim();
    void exeOpen();
    void exeOpenWait();
    void exeClose();
    void exeBite();
    void exeDamage();
    void exeDown();
    void exeDeadDamage();
    void exeDead();
    void exeAppearWait();
    void exeAppearDemo();
    void exePowerUpDemo();
    void exeDeadDemo();
    void exeBreakDemo();
    void exeDemoWait();
    void notifyAttack(SkeletalFishGuard*);
    LiveActor* getCurrentRail();
    f32 getBodyThickness() const;
    SkeletalFishBossHead* getHeadActor() const;
    void initLevelStatus();
    void initJoint();
    void initCollision();
    void initHead();
    void initScarFlash();
    void initBreakModel();
    void initSwitch(const TableIter&);
    void createGuards();
    void initShadow();
    void initCamera();
    void validateCollision();
    void invalidateCollision();
    void getMouthSensorCenterPos(Vec3f&, f32) const;
    void updateCollision();
    void powerUp();
    void startDamageAnim();
    void calcPlanetCenterPosition();
    bool calcGravityCenter(Vec3f*, const Vec3f&, const Vec3f&);
    void resetRail();
    void updateBgm();
    LiveActor* getCurrentBossRail();
    void stopScene(const char*, const Nerve*, SceneFunc);
    void startCamera(const char*);
    void resetCamera();
    void playDamageBrk();
    void startAppearDemo();
    void endAppearDemo();
    void startPowerUpDemo();
    void endPowerUpDemo();
    void startDeadDemo();
    void endBreakDemo();
    bool isEnableToBeDamaged() const;

    SkeletalFishBossInfo::LevelStatus* getLevelStatus() const {
        return mBossInfo->getLevelStatus(_110);
    }

    JointController* mControllers[0xD];
    s32* mJointIndicies;
    CollisionParts** mPartsArray;
    SkeletalFishBossHead* mBossHead;
    SkeletalFishBossScarFlash* mScarFlash;
    ModelObj* mBreakModel;
    const HitSensor* _D4;
    Pos3f _D8;
    SkeletalFishRailControl* mRailControl;
    SkeletalFishBossBattleDirector* mBossDirector;
    s32 _110;
    s32 _114;
    u32 _118;
    CameraTargetMtx* mCameraTargetMtx;
    Pos3f _120;
    Pos3f _150;
    Vec3f _180;
    SceneFunc mSceneFunc;
    const char* mCurScene;
    const Nerve* mSceneNerve;
    s32 _1A0;
    s32 _1A4;
    SkeletalFishBossInfo* mBossInfo;
    SkeletalFishGuardHolder* mGuardHolder;
    s32 _1B0;
    s32 _1B4;
};

class SkeletalFishBossScarFlash : public PartsModel {
public:
    SkeletalFishBossScarFlash(LiveActor*);

    virtual ~SkeletalFishBossScarFlash() {
    }

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void control();
};

class SkeletalFishBossHead : public PartsModel {
public:
    SkeletalFishBossHead(LiveActor*);

    virtual ~SkeletalFishBossHead() {
    }

    virtual void movement();
    virtual void calcAnim();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void updateCollisionMtx();
    void createSubModel();

    Mtx34f _9C;
    CollisionParts* _CC;
    Mtx34f _D0;
    CollisionParts* _100;
    ModelObj* mLightModels[2];
    ModelObj* mBloomModels[2];
    u8 _114;
};
