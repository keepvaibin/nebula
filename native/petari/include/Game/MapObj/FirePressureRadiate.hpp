#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class JointController;
struct JointControllerInfo;
class MsgSharedGroup;

class FirePressureRadiate : public LiveActor {
public:
    FirePressureRadiate(const char*);

    virtual ~FirePressureRadiate();
    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void updateHitSensor(HitSensor*);
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeRadiateMargin();
    void exeRadiate();
    void exePrepareToRadiate();
    inline void exeWait();
    void exeSyncWait();
    void exeRelax();
    void startWait();
    void startRelax();
    void calcRadiateEffectMtx();
    bool calcJointCannon(TPos3f*, const JointControllerInfo&);

    JointController* mJointController;
    TPos3f mRadiateMtx;
    f32 mCannonRotation;
    s32 mWaitTime;
    s32 mShootTime;
    s32 _CC;
    f32 _D0;
    f32 _D4;
    MsgSharedGroup* mGroup;
    bool _DC;
};
