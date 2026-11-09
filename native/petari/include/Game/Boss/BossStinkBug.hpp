#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class ActorJointCtrl;
class BossStinkBugActionSequencer;
class BossStinkBugBombHolder;
class CollisionParts;
class GroundChecker;
class PartsModel;

class BossStinkBug : public LiveActor {
public:
    enum SeVersion {
        SeVersion_NORMAL = 0,
        SeVersion_WATER = 1,
        SeVersion_ANGRY = 2,
        SeVersion_ANGRY_WATER = 3,
    };

    BossStinkBug(const char*);

    virtual void init(const TableIter&) override;
    virtual void calcAnim() override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void start();
    void initCamera(const TableIter&);
    void initSensor();
    void initCollision();
    void disposeBomb();
    void reuestMovementOnParts();
    LiveActor* getWingModel();
    LiveActor* getBombLauncher();
    void calcUpVec(Vec3f*) const;
    void updateAction();
    void updatePose();
    void setPose(MtxPtr);
    void updateCamera();
    bool isValidFollowId(s32) const;
    void validateCollisionGround();
    void validateCollisionFly();
    void validateCollisionHitOnly();
    void invalidateCollision();
    void onBindLeg();
    void offBindLeg();
    bool isLegInWater() const;
    void updateJointControl();
    void appearStarPiece(s32);
    void startEventCamera(const char*);
    void endEventCamera(const char*);
    bool isSensorBody(const HitSensor*) const;
    bool throwBomb(f32, f32);

    CollisionParts** mBodyCollisions;
    GroundChecker** mLegGroundCheckers;
    BossStinkBugBombHolder* mBombHolder;
    ActorJointCtrl* mJointCtrl;
    Pos3f mFollowMtx;
    ActorCameraInfo* mCameraInfo;
    BossStinkBugActionSequencer* mActionSequencer;
    PartsModel* mWingModel;
    PartsModel* mBombLauncher;
    Quat4f mPoseQuat;
    Vec3f mFront;
    Vec3f mCameraRegisterPos;
    s32 mCameraRegisterId;
    f32 mLegFriction;
    f32 mLegIKRate;
    bool mIsPoseUpdated;
    bool mIsLegBound;
};
