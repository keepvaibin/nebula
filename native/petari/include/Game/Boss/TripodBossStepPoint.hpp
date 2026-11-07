#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TripodBossStepPoint : public LiveActor {
public:
    /// @brief Creates a new `TripodBossStepPoint`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossStepPoint(const char* pName);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();

    void setStepPosition(const Vec3f&);
    void setStepNormal(const Vec3f&);
    void setStepFront(const Vec3f&);

    Vec3f mStepPosition;
    Vec3f mStepNormal;
    Vec3f mStepFront;
    s32 mLeg;
    s32 mSequencePriority;
    s32 mArg3;
    s32 mWaitTime;
};
