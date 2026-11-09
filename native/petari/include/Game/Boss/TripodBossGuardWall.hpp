#pragma once

#include "Game/Boss/TripodBossGuardWallPart.hpp"

class ActorCameraInfo;
class CameraTargetMtx;

class TripodBossGuardWall : public LiveActor {
public:
    TripodBossGuardWall(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void makeActorDead() override;
    virtual MtxPtr getBaseMtx() const override {
        return const_cast< MtxPtr >(mBaseMtx.mMtx);
    }
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

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
