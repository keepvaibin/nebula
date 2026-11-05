#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class TripodBossBreakMovement;

class TripodBossFixPartsBase : public LiveActor {
public:
    /// @brief Creates a new `TripodBossFixPartsBase`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossFixPartsBase(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void makeActorDead();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);
    virtual void calcTripodLocalMatrix(Pos3f*);
    virtual void activateTripodBoss();

    void initClippingSphere();
    void setClippingSphere(const Vec3f&, f32);
    void startBreakMovement();
    void updateBreakMovementMatrix();
    void updateTripodMatrix();
    void initBreakMovement(s32);

    Pos3f _8C;
    Vec3f _BC;
    TripodBossBreakMovement* mBreakMovement;
    s32 mJointID;
    Vec3f _D0;
    f32 mClippingRadius;
    bool _E0;
    bool _E1;
};
