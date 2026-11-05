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
    /// @brief Creates a new `BombTeresaHolder`.
    BombTeresaHolder();

    void disperseAll();
};

/// @brief Bouldergeist
class Polta : public LiveActor {
public:
    /// @brief Creates a new `Polta`.
    /// @param pName A pointer to the null-terminated name of the object.
    Polta(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void calcAnim();
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void makeActorDead();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initArm();
    void initPartsModel();
    void initSensor();
    void initBombTeresaHolder();
    void initCamera(const JMapInfoIter&);
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
    Quat4f _B4;
    Vec3f _C4;
    Quat4f _D0;
    Vec3f _E0;
    f32 _EC;
};
