#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class JointController;
struct JointControllerInfo;
class MsgSharedGroup;

class FirePressureRadiate : public LiveActor {
public:
    FirePressureRadiate(const char*);

    virtual void init(const TableIter&);
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
    bool calcJointCannon(Pos3f*, const JointControllerInfo&);

    JointController* mJointController;
    Pos3f mRadiateMtx;
    f32 mCannonRotation;
    s32 mWaitTime;
    s32 mShootTime;
    s32 mRadiateStep;
    f32 mRadiateLength;
    f32 mRadiateMaxLength;
    MsgSharedGroup* mGroup;
    bool mIsGroupLeader;
};
