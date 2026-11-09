#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class TripodBossBreakMovement;

class TripodBossFixPartsBase : public LiveActor {
public:
    TripodBossFixPartsBase(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void makeActorDead() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;
    virtual void calcTripodLocalMatrix(Pos3f*);
    virtual void activateTripodBoss();

    void initClippingSphere();
    void setClippingSphere(const Vec3f& rCenter, f32 radius);
    void startBreakMovement();
    void updateBreakMovementMatrix();
    void updateTripodMatrix();
    void initBreakMovement(s32);

    Pos3f mMtx;
    Vec3f mLocalPos;
    TripodBossBreakMovement* mBreakMovement;
    s32 mJointID;
    Vec3f mClipCenter;
    f32 mClippingRadius;
    bool mIsActive;
};
