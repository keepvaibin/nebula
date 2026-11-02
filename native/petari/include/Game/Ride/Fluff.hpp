#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class CameraTargetMtx;
class ModelObj;

class Fluff : public LiveActor {
public:
    Fluff(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeFreeBloom();
    void exeFreeWaitOnGround();
    void exeFreeWaitAir();
    void exeFreeWaitInvalid();
    void exeRideStart();
    void exeRideFly();
    void exeRideFlyOnWind();
    void exeRideSpinUp();
    void exeRideSpinUpOnWind();

    bool updateRide();
    void updateWind();
    bool trySpinUp(s32 step);
    void endBind(f32 ejectForce);

    TVec3f mSide;
    TVec3f mUp;
    TVec3f mFront;
    TPos3f mBaseMtx;
    TVec3f mTargetUpVec;
    TVec3f mWindDir;
    f32 mWindDistance;
    s32 mWindSpinTimer;
    s32 mSpinsRemaining;
    ModelObj* mModel;
    LiveActor* mRider;
    CameraTargetMtx* mCameraTargetMtx;
};
