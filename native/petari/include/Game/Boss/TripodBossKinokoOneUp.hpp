#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class BenefitItemOneUp;

class TripodBossKinokoOneUp : public LiveActor {
public:
    /// @brief Creates a new `TripodBossKinokoOneUp`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossKinokoOneUp(const char* pName);

    virtual void init(const JMapInfoIter&);

    virtual MtxPtr getBaseMtx() const {
        return (MtxPtr)&_BC;
    }

    virtual void control();

    void exeActive();
    void exeEnd();

    TPos3f _8C;
    TPos3f _BC;
    BenefitItemOneUp* mOneUp;
    s32 mJointID;
};
