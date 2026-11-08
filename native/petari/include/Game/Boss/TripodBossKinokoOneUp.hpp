#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class BenefitItemOneUp;

class TripodBossKinokoOneUp : public LiveActor {
public:
    /// @brief Creates a new `TripodBossKinokoOneUp`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossKinokoOneUp(const char* pName);

    virtual void init(const TableIter&);

    virtual MtxPtr getBaseMtx() const {
        return (MtxPtr)&mMtx;
    }

    virtual void control();

    void exeActive();
    void exeEnd();

    Pos3f mLocalMtx;
    Pos3f mMtx;
    BenefitItemOneUp* mOneUp;
    s32 mJointID;
};
