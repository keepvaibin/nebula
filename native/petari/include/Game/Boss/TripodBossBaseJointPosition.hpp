#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class TripodBossBaseJointPosition : public LiveActor {
public:
    /// @brief Creates a new `TripodBossBaseJointPosition`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossBaseJointPosition(const char* pName);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();

    virtual MtxPtr getBaseMtx() const {
        return (MtxPtr)&mMtx;
    }

    virtual void control();

    s32 mID;
    Pos3f mMtx;
};
