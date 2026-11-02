#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

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
    virtual void calcTripodLocalMatrix(TPos3f*);
    virtual void activateTripodBoss();

    void initClippingSphere();
    void setClippingSphere(const TVec3f&, f32);
    void startBreakMovement();
    void updateBreakMovementMatrix();
    void updateTripodMatrix();
    void initBreakMovement(s32);

    TPos3f _8C;
    TVec3f _BC;
    TripodBossBreakMovement* mBreakMovement;
    s32 mJointID;
    TVec3f _D0;
    f32 mClippingRadius;
    bool _E0;
    bool _E1;
};
