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

    virtual void init(const JMapInfoIter&);
    virtual void calcAnim();
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void makeActorDead();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void start();
    void initCamera(const JMapInfoIter&);
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

    CollisionParts** _8C;
    GroundChecker** _90;
    BossStinkBugBombHolder* _94;
    ActorJointCtrl* _98;
    Pos3f _9C;
    ActorCameraInfo* _CC;
    BossStinkBugActionSequencer* mActionSequencer;
    PartsModel* _D4;
    PartsModel* mBombLauncher;
    Quat4f _DC;
    Vec3f _EC;
    Vec3f _F8;
    s32 _104;
    f32 _108;
    f32 _10C;
    bool _110;
    bool _111;
};
