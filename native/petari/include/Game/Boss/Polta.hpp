#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class BombTeresa;
class ModelObj;
class PoltaActionSequencer;
class PoltaArm;
class PoltaGroundRockHolder;
class PoltaRockHolder;
class PoltaSensorCtrl;

class BombTeresaHolder : public DeriveActorGroup< BombTeresa > {
public:
    BombTeresaHolder();

    void disperseAll();
};

/// @brief Bouldergeist
class Polta : public LiveActor {
public:
    Polta(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void calcAnim() override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void initArm();
    void initPartsModel();
    void initSensor();
    void initBombTeresaHolder();
    void initCamera(const TableIter&);
    void appearBreakModel(const Vec3f&, const char*);
    void appearBreakModelFirst(const Vec3f&);
    void appearBreakModelMiddle(const Vec3f&);
    void appearBreakModelLast(const Vec3f&);
    void appearStarPiece(s32);
    bool isEndBreakModel() const;
    void killBreakModel();
    void rotateToPlayer();
    f32 calcToPlayerRotateSpeed() const;
    void updateAction();
    void updatePose(f32, f32);
    void start();
    void setStartPose();

    PoltaSensorCtrl* mSensorCtrl;
    PoltaArm* mLeftArm;
    PoltaArm* mRightArm;
    PoltaActionSequencer* mSequencer;
    BombTeresaHolder* mBombTeresaHolder;
    PoltaRockHolder* mRockHolder;
    PoltaGroundRockHolder* mGroundRockHolder;
    ActorCameraInfo* mCameraInfo;
    ModelObj* mBreakModel;
    ModelObj* mFormationModel;
    Quat4f mPoseQuat;
    Vec3f mFront;
    Quat4f mStartQuat;
    Vec3f mStartPos;
    f32 mRotateSpeed;
};
