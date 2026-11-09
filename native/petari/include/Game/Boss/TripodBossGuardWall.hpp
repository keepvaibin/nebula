#pragma once

#include "Game/Boss/TripodBossGuardWallPart.hpp"

class ActorCameraInfo;
class CameraTargetMtx;

class TripodBossGuardWall : public LiveActor {
public:
    TripodBossGuardWall(const char* pName);

    virtual void init(const TableIter&);
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void makeActorDead();
    virtual MtxPtr getBaseMtx() const {
        return const_cast< MtxPtr >(mBaseMtx.mMtx);
    }
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initParts();
    void requestStart();
    void updateMatrix();
    void updateCameraTarget();

    void exeWait();
    void exeTryDemo();
    void exeDemo();
    void exeRotate();

    CameraTargetMtx* mCameraTargetMtx;
    TripodBossGuardWallPart mParts[8];
    Pos3f mBaseMtx;
    Vec3f mStartTrans;
    s32 mJointID;
    ActorCameraInfo* mCameraInfo;
};
