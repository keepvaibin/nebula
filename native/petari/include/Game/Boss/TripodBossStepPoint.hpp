#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TripodBossStepPoint : public LiveActor {
public:
    TripodBossStepPoint(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;

    void setStepPosition(const Vec3f&);
    void setStepNormal(const Vec3f&);
    void setStepFront(const Vec3f&);

    Vec3f mStepPosition;
    Vec3f mStepNormal;
    Vec3f mStepFront;
    s32 mLeg;
    s32 mSequencePriority;
    s32 mSequenceIndex;  // -1: in no sequence
    s32 mWaitTime;
};
