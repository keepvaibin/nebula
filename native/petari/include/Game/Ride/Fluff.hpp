#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class CameraTargetMtx;
class ModelObj;

class Fluff : public LiveActor {
public:
    Fluff(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeFreeBloom();
    void exeFreeWaitOnGround();
    void exeFreeWaitAir();
    void exeFreeWaitInvalid();
    void exeRideStart();
    void exeRideFly();
    void exeRideFlyOnWind();
    void exeRideSpinUp();

    bool updateRide();
    void updateWind();
    bool trySpinUp(s32 step);
    void endBind(f32 ejectForce);

    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    Pos3f mBaseMtx;
    Vec3f mTargetUpVec;
    Vec3f mWindDir;
    f32 mWindDistance;
    s32 mWindSpinTimer;
    s32 mSpinsRemaining;
    ModelObj* mModel;
    LiveActor* mRider;
    CameraTargetMtx* mCameraTargetMtx;
};
