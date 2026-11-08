#pragma once

#include "Game/Util/Array.hpp"

class TripodBossStepPoint;

// The step points of one sequence, lowest priority first; it grows with what placement adds.
class TripodBossStepSequence {
public:
    TripodBossStepSequence();

    void addStepPoint(TripodBossStepPoint*);
    s32 getCurrentLeg() const;
    s32 getCurrentWaitTime() const;
    TripodBossStepPoint* getCurrentStepPoint();
    void nextStep();
    bool isEmpty() const;
    void reset();

    MR::AssignableArray< TripodBossStepPoint* > mStepPoints;
    s32 mCurrentPoint;
    s32 mPointNum;
};
