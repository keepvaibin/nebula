#pragma once

#include "Game/LiveActor/ModelObj.hpp"

class ActorCameraInfo;

class SunakazeKunTrap : public ModelObj {
public:
    SunakazeKunTrap(LiveActor*, const Vec3f&);

    virtual void init(const TableIter& rIter) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void exeTrap();

private:
    LiveActor* mHost;
    Vec3f mLocalPos;
};

class Sandstorm : public LiveActor {
public:
    enum Type {
        Type_TwisterSan,
        Type_SunakazeKun,
        Type_TwisterSanSlim,
    };

    Sandstorm(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void updateHitSensor(HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;


    void initSandstormParam();
    void initTrapModel();
    void updateBindActorMtx();
    void endBind();
    void updateSpiral();
    bool tryStartBind(HitSensor*);
    bool tryTornadoJumpPlayer();
    bool tryNonActive();
    bool isSunakazeKun() const;

    void exeNonActive();
    void exeWait();
    void exeStorm();
    void exeStormAdjust();
    void exeStormFix();
    void exeStormEnd();

private:
    ActorCameraInfo* mCameraInfo;
    LiveActor* mBindActor;
    Vec3f mFront;
    Vec3f mClippingCenter;
    s32 mType;
    f32 mSpiralRot;
    f32 mSpiralRadius;
    f32 mSpiralAngle;
    f32 mBindRadius;
    f32 mAdjustStartRadius;
    f32 mBindOffsetY;
    s32 mBindStep;
    f32 mBindStartStep;
    f32 mSensorHeight;
    f32 mSizeUpRate;
    f32 mSensorMin;
    s32 mStepStorm;
    s32 mStepAdjust;
    s32 mStepFix;
    s32 mStepEnd;
    f32 mBindHeight;
    SunakazeKunTrap** mTrapModel;
};
