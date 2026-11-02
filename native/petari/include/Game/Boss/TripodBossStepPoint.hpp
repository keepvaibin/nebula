#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TripodBossStepPoint : public LiveActor {
public:
    /// @brief Creates a new `TripodBossStepPoint`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossStepPoint(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();

    void setStepPosition(const TVec3f&);
    void setStepNormal(const TVec3f&);
    void setStepFront(const TVec3f&);

    TVec3f mStepPosition;
    TVec3f mStepNormal;
    TVec3f mStepFront;
    s32 mLeg;
    s32 mSequencePriority;
    s32 mArg3;
    s32 mWaitTime;
};
